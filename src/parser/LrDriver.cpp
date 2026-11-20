// T2.8 LR 驱动 + ParseTree 构建实现（LrDriver）
// 算法（docs/PROGRESS.md 决策点 4 / 决策点 5 B + DATA_STRUCTURES §5.6）：
//   1) 防御校验 + EOF 归一化（缺则本地追加 EOF）+ token.code→终结符 symbolId 映射建表。
//   2) stateStack/nodeStack 同步推进：Shift→叶子；Reduce→内部节点（children 对齐右部，
//      注释见下）；Accept→返回；Error→遇错即停（决策点 5 B）。
//   3) 死循环防御：maxSteps = max(1000, tokens.size() * 1000)。

#include "LrDriver.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>

namespace compiler::parser {
namespace {

Diagnostic makeError(const std::string& msg, const SourceLocation& loc)
{
    Diagnostic d;
    d.severity = Severity::Error;
    d.stage = "parse";
    d.message = msg;
    d.loc = loc;
    return d;
}

std::string quote(const std::string& s)
{
    return "\"" + s + "\"";
}

} // namespace

Result<ParseTree> LrDriver::parse(const Grammar& g, const ParseTableData& table,
                                  const std::vector<Token>& tokens) const
{
    // ---- 防御校验 ----
    if (g.startSymbol < 0 || !g.symbol(g.startSymbol)) {
        return Result<ParseTree>::failure(makeError("开始符号缺失（startSymbol 无效）",
                                                    SourceLocation{-1, -1, -1}));
    }
    if (g.productions.empty()) {
        return Result<ParseTree>::failure(makeError("文法不含任何产生式",
                                                    SourceLocation{-1, -1, -1}));
    }
    if (table.action.empty() || table.action.size() != table.gotoTable.size()) {
        return Result<ParseTree>::failure(makeError("分析表为空或尺寸不一致（ParseTableData 无效）",
                                                    SourceLocation{-1, -1, -1}));
    }

    const auto internalError = [](const std::string& msg) {
        return Result<ParseTree>::failure(makeError(msg, SourceLocation{-1, -1, -1}));
    };

    // ---- EOF 归一化：缺末尾 EOF 时本地追加（不引入 lexer 依赖） ----
    std::vector<Token> norm = tokens;
    if (norm.empty() || !norm.back().isEof()) {
        Token eof;
        eof.code = 0;
        eof.kind = TokenKind::EndOfFile;
        eof.name = "EOF";
        eof.lexeme = "$";
        eof.loc = {-1, -1, -1};
        norm.push_back(std::move(eof));
    }

    // ---- token.code → 终结符 symbolId 映射表（由 Symbol.tokenCode 建；$ 亦含） ----
    std::map<int, int> codeToSym;
    for (const Symbol& s : g.symbols) {
        if ((s.kind == SymbolKind::Terminal || s.kind == SymbolKind::EndOfFile) &&
            s.tokenCode >= 0) {
            codeToSym[s.tokenCode] = s.id;
        }
    }

    // ---- 意外 token / ACTION 缺省格错误（含期望终结符集合与状态 id） ----
    const auto unexpectedError = [&](int top, const Token& cur, size_t pos) {
        std::string msg = "语法错误：意外的 token " + quote(cur.lexeme) +
                          "（code=" + std::to_string(cur.code) +
                          ", tokenIndex=" + std::to_string(pos) +
                          ", 状态=" + std::to_string(top) + "）；期望的终结符：";
        bool first = true;
        for (const auto& kv : table.action[static_cast<size_t>(top)]) {
            if (!first) {
                msg += ", ";
            }
            first = false;
            const Symbol* s = g.symbol(kv.first);
            msg += s ? s->name : ("#" + std::to_string(kv.first));
        }
        return Result<ParseTree>::failure(makeError(msg, cur.loc));
    };

    ParseTree tree;
    tree.tokens = norm;

    std::vector<int> stateStack;
    std::vector<std::unique_ptr<ParseTreeNode>> nodeStack;
    stateStack.push_back(0);

    const size_t maxSteps = std::max<size_t>(1000, norm.size() * 1000);
    size_t step = 0;
    size_t pos = 0;

    while (true) {
        if (++step > maxSteps) {
            return internalError("分析步数超过上限（疑似死循环），已中止");
        }
        // 状态栈比节点栈多 1（初始状态 0 无对应符号节点）；任何同步破坏即内部不一致
        if (stateStack.size() != nodeStack.size() + 1) {
            return internalError("内部不一致：状态栈与节点栈不同步");
        }
        if (pos >= norm.size()) {
            return internalError("内部不一致：token 下标越界");
        }

        const int top = stateStack.back();
        const Token& cur = norm[pos];

        // 当前 token.code → 终结符 symbolId；无映射 → 语法错误
        const auto codeIt = codeToSym.find(cur.code);
        if (codeIt == codeToSym.end()) {
            return Result<ParseTree>::failure(makeError(
                "无法映射到终结符的 token: code=" + std::to_string(cur.code) +
                " lexeme=" + quote(cur.lexeme) +
                "（tokenIndex=" + std::to_string(pos) + "）", cur.loc));
        }
        const int sym = codeIt->second;

        // ACTION 缺省格 = Error
        const auto cellIt = table.action[static_cast<size_t>(top)].find(sym);
        if (cellIt == table.action[static_cast<size_t>(top)].end()) {
            return unexpectedError(top, cur, pos);
        }
        const Action& act = cellIt->second;

        if (act.type == ActionType::Shift) {
            // Shift：建叶子节点并绑定当前 token（tokenIndex = 归一化 token 流下标）
            auto leaf = std::make_unique<ParseTreeNode>();
            leaf->symbolId = sym;
            leaf->productionId = -1;
            leaf->tokenIndex = static_cast<int>(pos);
            leaf->text = cur.lexeme;
            stateStack.push_back(act.target);
            nodeStack.push_back(std::move(leaf));
            ++pos;
        } else if (act.type == ActionType::Reduce) {
            const Production& p = g.productions[static_cast<size_t>(act.target)];
            const size_t rhsSize = p.rhs.size();
            if (stateStack.size() <= rhsSize) {
                return internalError("内部不一致：归约弹出数量超过状态栈");
            }
            // 弹 |rhs| 个状态；nodeStack 连续 pop_back 得逆序 [D,C,B]（产生式 A -> B C D）
            stateStack.resize(stateStack.size() - rhsSize);
            std::vector<std::unique_ptr<ParseTreeNode>> popped;
            popped.reserve(rhsSize);
            for (size_t k = 0; k < rhsSize; ++k) {
                popped.push_back(std::move(nodeStack.back()));
                nodeStack.pop_back();
            }
            // children 顺序必须与产生式右部一致：reverse 后 [B,C,D] 再 move 进父节点
            std::reverse(popped.begin(), popped.end());

            auto node = std::make_unique<ParseTreeNode>();
            node->symbolId = p.lhs;
            node->productionId = act.target;
            node->tokenIndex = -1;
            const Symbol* lhsSym = g.symbol(p.lhs);
            node->text = lhsSym ? lhsSym->name : "";
            node->children = std::move(popped);   // ε 产生式（rhsSize=0）：children 为空

            const auto gIt = table.gotoTable[static_cast<size_t>(stateStack.back())].find(p.lhs);
            if (gIt == table.gotoTable[static_cast<size_t>(stateStack.back())].end()) {
                return internalError("内部不一致：归约后 GOTO 缺省");
            }
            stateStack.push_back(gIt->second);
            nodeStack.push_back(std::move(node));
        } else if (act.type == ActionType::Accept) {
            // Accept：接受时节点栈应恰有 1 个节点（开始符号子树）
            if (nodeStack.size() != 1) {
                return internalError("内部不一致：接受时节点栈应恰有 1 个节点");
            }
            tree.root = std::move(nodeStack.back());
            nodeStack.pop_back();
            return Result<ParseTree>::success(std::move(tree));
        } else {
            return unexpectedError(top, cur, pos);
        }
    }
}

} // namespace compiler::parser
