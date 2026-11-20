// T2.8 单测：LrDriver（LR 驱动 + ParseTree 构建）。
// 零 Qt、纯 STL；沿用 T1.x/T2.x 冒烟约定：g++ 独立编译运行，退出码 0 = 全部通过。
// 用法：lr_driver_test <languages 根目录>（缺省 ./test/languages）
//
// 覆盖（docs/PROGRESS.md 决策点 4 / 决策点 5 B + DATA_STRUCTURES §5.6 + 方案 A-H）：
//   TINY / Mini-C 全链路：rules(registry) → bnf → 化简 → First/Follow → LR(0) → LALR(1)
//     → 分析表 → Scanner(test.tiny) → LrDriver → ParseTree；
//     根节点 symbolId/productionId 锚点、叶子回环（== 输入 token 去末尾 EOF）、tokenIndex 有序；
//   手工 S->a（Shift/Reduce/Accept 与 children 对齐）、纯 ε S->#（rhsSize=0 children 为空）；
//   错误路径：无映射 token（code=999，message 含指定文案）、ACTION 缺省格（含期望终结符）、
//     删 token 后遇错即停（stage="parse"）；死循环防御（手工表 Reduce 自环 → maxSteps 中止）。

#include "BnfParser.h"
#include "FirstFollow.h"
#include "GrammarSimplifier.h"
#include "Lalr1Builder.h"
#include "LexerBuilder.h"
#include "LexRulesParser.h"
#include "Lr0Automaton.h"
#include "LrDriver.h"
#include "ParseTable.h"
#include "Scanner.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

using compiler::Symbol;
using compiler::SymbolKind;
using compiler::Token;
using compiler::TokenKind;
using compiler::TokenCodeRegistry;
using compiler::grammar::BnfParser;
using compiler::grammar::FirstFollow;
using compiler::grammar::GrammarSimplifier;
using compiler::lexer::LexerBuilder;
using compiler::lexer::LexRulesParser;
using compiler::lexer::Scanner;
using compiler::parser::Action;
using compiler::parser::ActionType;
using compiler::parser::Lalr1Builder;
using compiler::parser::Lr0Automaton;
using compiler::parser::LrDriver;
using compiler::parser::ParseTableBuilder;
using compiler::parser::ParseTableData;
using compiler::ParseTree;
using compiler::ParseTreeNode;

int g_checks = 0;

#define CHECK(...)                                                            \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(__VA_ARGS__)) {                                                 \
            std::cerr << "FAIL @" << __LINE__ << ": " << #__VA_ARGS__ << std::endl; \
            return false;                                                     \
        }                                                                     \
    } while (0)

std::string loadFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

// 手工构造 Grammar（满足 BnfParser 不变式：symbols[i].id==i、productions[i].id==i；
// 预置 $=0 / #=1；用户符号自 id 2 起；{lhs} 表示 ε 产生式）。
compiler::Grammar makeHandGrammar(
    const std::vector<std::pair<std::string, SymbolKind>>& userSymbols,
    const std::vector<std::vector<int>>& productions, int startSymbol)
{
    compiler::Grammar g;
    g.symbols.push_back({0, "$", SymbolKind::EndOfFile, 0});
    g.symbols.push_back({1, "#", SymbolKind::Empty, -1});
    for (size_t i = 0; i < userSymbols.size(); ++i) {
        g.symbols.push_back({static_cast<int>(i + 2), userSymbols[i].first,
                             userSymbols[i].second, -1});
    }
    for (size_t i = 0; i < productions.size(); ++i) {
        compiler::Production p;
        p.id = static_cast<int>(i);
        if (productions[i].empty()) {
            continue;
        }
        p.lhs = productions[i][0];
        for (size_t k = 1; k < productions[i].size(); ++k) {
            p.rhs.push_back(productions[i][k]);
        }
        g.productions.push_back(std::move(p));
    }
    g.startSymbol = startSymbol;
    return g;
}

bool hasStageError(const compiler::Result<ParseTree>& r, const std::string& stage)
{
    for (const auto& d : r.diagnostics) {
        if (d.severity == compiler::Severity::Error && d.stage == stage) {
            return true;
        }
    }
    return false;
}

int idOf(const compiler::Grammar& g, const std::string& name, SymbolKind kind)
{
    return g.symbolId(name, kind);
}

// 按 lhs + rhs 精确反查产生式 id（不硬编码；找不到返回 -1）
int findProduction(const compiler::Grammar& g, int lhs, const std::vector<int>& rhs)
{
    for (const auto& p : g.productions) {
        if (p.lhs == lhs && p.rhs == rhs) {
            return p.id;
        }
    }
    return -1;
}

// 手工文法：终结符 tokenCode 取 symbolId（token.code → symbolId 映射用）
void assignTerminalCodes(compiler::Grammar& g)
{
    for (Symbol& s : g.symbols) {
        if (s.kind == SymbolKind::Terminal) {
            s.tokenCode = s.id;
        }
    }
}

Token makeTok(int code, const std::string& name, const std::string& lexeme)
{
    Token t;
    t.code = code;
    t.name = name;
    t.lexeme = lexeme;
    t.kind = TokenKind::Unknown;
    return t;
}

// 全链路（Grammar 已含终结符 tokenCode）→ 分析表
compiler::Result<ParseTableData> buildTableFor(const compiler::Grammar& g)
{
    auto ff = FirstFollow().compute(g);
    if (!ff.ok) {
        return {};
    }
    auto lr0 = Lr0Automaton().build(g);
    if (!lr0.ok) {
        return {};
    }
    auto lr = Lalr1Builder().build(g, ff.value, lr0.value);
    if (!lr.ok) {
        return {};
    }
    return ParseTableBuilder().build(g, lr.value);
}

// 语言全链路产物（grammar + 表 + 扫描 token 流（含 EOF））
struct FullChain {
    bool ok = false;
    compiler::Grammar g;
    ParseTableData table;
    TokenCodeRegistry registry;
    std::vector<Token> tokens;   // Scanner 产出，含末尾 EOF
};

FullChain buildChain(const std::string& langRoot, const std::string& lang)
{
    FullChain ch;
    LexRulesParser rp;
    auto rr = rp.parseFile(langRoot + "/" + lang + "/" + lang + ".rules");
    if (!rr.ok) {
        return ch;
    }
    ch.registry = rr.value.registry;

    auto bnf = BnfParser().parseFile(langRoot + "/" + lang + "/" + lang + ".bnf",
                                     &ch.registry);
    if (!bnf.ok) {
        return ch;
    }
    auto sg = GrammarSimplifier().simplify(bnf.value);
    if (!sg.ok) {
        return ch;
    }
    ch.g = sg.value.grammar;

    auto table = buildTableFor(ch.g);
    if (!table.ok) {
        return ch;
    }
    ch.table = table.value;

    const std::string srcPath = langRoot + "/" + lang + "/samples/test." + lang;
    LexerBuilder builder;
    auto dfar = builder.buildMinimalDfa(rr.value);
    if (!dfar.ok) {
        return ch;
    }
    Scanner scanner;
    if (!scanner.configure(rr.value, dfar.value)) {
        return ch;
    }
    auto sr = scanner.scan(loadFile(srcPath), srcPath);
    if (!sr.ok) {
        return ch;
    }
    ch.tokens = std::move(sr.tokens);
    ch.ok = true;
    return ch;
}

// 中序收集叶子 tokenIndex（回环断言用）
void collectLeafIndexes(const ParseTreeNode* n, std::vector<int>& out)
{
    if (!n) {
        return;
    }
    // 真叶子 = productionId == -1（ε 内部节点 children 也为空，但 productionId >= 0，不算叶子）
    if (n->productionId == -1) {
        out.push_back(n->tokenIndex);
        return;
    }
    for (const auto& c : n->children) {
        collectLeafIndexes(c.get(), out);
    }
}

// 回环断言：叶子 token 序列 == expectedNoEof（输入 token 去掉末尾 EOF）
bool checkLeafRoundTrip(const ParseTree& tree, const std::vector<Token>& expectedNoEof)
{
    std::vector<int> idxs;
    collectLeafIndexes(tree.root.get(), idxs);
    if (idxs.size() != expectedNoEof.size()) {
        return false;
    }
    for (size_t i = 0; i < idxs.size(); ++i) {
        if (idxs[i] < 0 || static_cast<size_t>(idxs[i]) >= tree.tokens.size()) {
            return false;
        }
        const Token& t = tree.tokens[static_cast<size_t>(idxs[i])];
        const Token& e = expectedNoEof[i];
        if (t.code != e.code || t.lexeme != e.lexeme) {
            return false;
        }
    }
    return true;
}

// E-1：TINY 全链路（根 program、叶子回环、tokenIndex 有序）
bool testTiny(const std::string& langRoot)
{
    FullChain ch = buildChain(langRoot, "tiny");
    CHECK(ch.ok);

    auto pr = LrDriver().parse(ch.g, ch.table, ch.tokens);
    CHECK(pr.ok);
    const ParseTree& tree = pr.value;

    const int program = idOf(ch.g, "program", SymbolKind::Nonterminal);
    const int stmtSeq = idOf(ch.g, "stmt-sequence", SymbolKind::Nonterminal);
    const int programProd = findProduction(ch.g, program, {stmtSeq});
    CHECK(programProd >= 0);

    CHECK(tree.root != nullptr);
    CHECK(tree.root->symbolId == program);
    CHECK(tree.root->productionId == programProd);
    CHECK(!tree.root->children.empty());

    // 回环：叶子 == 输入 token 去末尾 EOF
    std::vector<Token> noEof = ch.tokens;
    if (!noEof.empty() && noEof.back().isEof()) {
        noEof.pop_back();
    }
    CHECK(!noEof.empty());
    CHECK(checkLeafRoundTrip(tree, noEof));

    // tokenIndex 严格递增（叶子按源顺序）
    std::vector<int> idxs;
    collectLeafIndexes(tree.root.get(), idxs);
    for (size_t i = 1; i < idxs.size(); ++i) {
        CHECK(idxs[i] == idxs[i - 1] + 1);
    }

    std::cout << "info[tiny] tokens=" << noEof.size()
              << " leaves=" << idxs.size()
              << " root_prod=" << tree.root->productionId << std::endl;
    return true;
}

// E-2：Mini-C 全链路（根 program、叶子回环、嵌套结构树深 > 0）
bool testMinic(const std::string& langRoot)
{
    FullChain ch = buildChain(langRoot, "minic");
    CHECK(ch.ok);

    auto pr = LrDriver().parse(ch.g, ch.table, ch.tokens);
    CHECK(pr.ok);
    const ParseTree& tree = pr.value;

    const int program = idOf(ch.g, "program", SymbolKind::Nonterminal);
    const int defList = idOf(ch.g, "definition-list", SymbolKind::Nonterminal);
    const int programProd = findProduction(ch.g, program, {defList});
    CHECK(programProd >= 0);

    CHECK(tree.root != nullptr);
    CHECK(tree.root->symbolId == program);
    CHECK(tree.root->productionId == programProd);
    CHECK(!tree.root->children.empty());

    std::vector<Token> noEof = ch.tokens;
    if (!noEof.empty() && noEof.back().isEof()) {
        noEof.pop_back();
    }
    CHECK(!noEof.empty());
    CHECK(checkLeafRoundTrip(tree, noEof));

    std::cout << "info[minic] tokens=" << noEof.size()
              << " root_prod=" << tree.root->productionId << std::endl;
    return true;
}

// E-3：单产生式 S->a（Shift 叶子 → Reduce 内部节点 → Accept；children 对齐）
bool testSingleProduction()
{
    auto g = makeHandGrammar(
        { {"S", SymbolKind::Nonterminal}, {"a", SymbolKind::Terminal} },
        { {2, 3} }, 2);
    assignTerminalCodes(g);
    auto tr = buildTableFor(g);
    CHECK(tr.ok);

    std::vector<Token> in = { makeTok(3, "a", "a"), makeTok(0, "EOF", "$") };
    auto pr = LrDriver().parse(g, tr.value, in);
    CHECK(pr.ok);
    const ParseTree& tree = pr.value;

    CHECK(tree.root != nullptr);
    CHECK(tree.root->symbolId == 2);              // S
    CHECK(tree.root->productionId == 0);          // S -> a
    CHECK(tree.root->children.size() == 1);
    const auto& child = tree.root->children[0];
    CHECK(child->isLeaf());
    CHECK(child->symbolId == 3);                  // a
    CHECK(child->tokenIndex == 0);
    CHECK(child->text == "a");
    CHECK(checkLeafRoundTrip(tree, std::vector<Token>(in.begin(), in.end() - 1)));
    return true;
}

// E-4：纯 ε S->#（rhsSize=0：内部节点 children 为空；无叶子）
bool testEpsilonOnly()
{
    auto g = makeHandGrammar(
        { {"S", SymbolKind::Nonterminal} },
        { {2} }, 2);                              // S -> ε
    assignTerminalCodes(g);
    auto tr = buildTableFor(g);
    CHECK(tr.ok);

    std::vector<Token> in = { makeTok(0, "EOF", "$") };
    auto pr = LrDriver().parse(g, tr.value, in);
    CHECK(pr.ok);
    const ParseTree& tree = pr.value;

    CHECK(tree.root != nullptr);
    CHECK(tree.root->symbolId == 2);
    CHECK(tree.root->productionId == 0);
    CHECK(tree.root->children.empty());           // ε 产生式 children 为空
    CHECK(checkLeafRoundTrip(tree, {}));          // 无叶子
    return true;
}

// E-5：错误 —— token.code 无终结符映射（message 含指定文案）
bool testErrorUnmapped()
{
    auto g = makeHandGrammar(
        { {"S", SymbolKind::Nonterminal}, {"a", SymbolKind::Terminal} },
        { {2, 3} }, 2);
    assignTerminalCodes(g);
    auto tr = buildTableFor(g);
    CHECK(tr.ok);

    std::vector<Token> in = { makeTok(999, "X", "x"), makeTok(0, "EOF", "$") };
    auto pr = LrDriver().parse(g, tr.value, in);
    CHECK(!pr.ok);
    CHECK(hasStageError(pr, "parse"));
    bool found = false;
    for (const auto& d : pr.diagnostics) {
        if (d.message.find("无法映射到终结符的 token: code=999 lexeme=\"x\"") !=
            std::string::npos) {
            found = true;
        }
    }
    CHECK(found);
    return true;
}

// E-6：错误 —— ACTION 缺省格（期望终结符集合 + 状态 id）
bool testErrorActionMissing()
{
    auto g = makeHandGrammar(
        { {"S", SymbolKind::Nonterminal}, {"a", SymbolKind::Terminal} },
        { {2, 3} }, 2);
    assignTerminalCodes(g);
    auto tr = buildTableFor(g);
    CHECK(tr.ok);

    // S->a：读入 a 归约后进入接受态（状态 1，只有 [$]=Accept）；第二个 a → 缺省格
    std::vector<Token> in = { makeTok(3, "a", "a"), makeTok(3, "a", "a"),
                              makeTok(0, "EOF", "$") };
    auto pr = LrDriver().parse(g, tr.value, in);
    CHECK(!pr.ok);
    CHECK(hasStageError(pr, "parse"));
    bool found = false;
    for (const auto& d : pr.diagnostics) {
        if (d.message.find("意外的 token") != std::string::npos &&
            d.message.find("期望的终结符") != std::string::npos &&
            d.message.find("状态=") != std::string::npos) {
            found = true;
        }
    }
    CHECK(found);
    return true;
}

// E-7：错误 —— 合法流删一个 token → 遇错即停（决策点 5 B），不崩溃
bool testErrorDeleteToken(const std::string& langRoot)
{
    FullChain ch = buildChain(langRoot, "minic");
    CHECK(ch.ok);
    CHECK(ch.tokens.size() >= 3);

    std::vector<Token> bad = ch.tokens;
    bad.erase(bad.end() - 2);   // 删掉倒数第二个（保留 EOF）
    auto pr = LrDriver().parse(ch.g, ch.table, bad);
    CHECK(!pr.ok);
    CHECK(hasStageError(pr, "parse"));
    return true;
}

// E-8：死循环防御 —— 手工表 Reduce 自环（goto[0][S]=0）→ maxSteps 中止
bool testLoopDefense()
{
    auto g = makeHandGrammar(
        { {"S", SymbolKind::Nonterminal} },
        { {2} }, 2);                              // S -> ε（rhsSize=0）
    assignTerminalCodes(g);

    // 手工表：状态 0 在 $ 上 Reduce(0)，goto[0][S]=0 → 永不消费 token 的自环
    ParseTableData pt;
    pt.action.assign(1, {});
    pt.gotoTable.assign(1, {});
    pt.action[0][0] = Action{ ActionType::Reduce, 0 };
    pt.gotoTable[0][2] = 0;                       // S id = 2

    std::vector<Token> in = { makeTok(0, "EOF", "$") };
    auto pr = LrDriver().parse(g, pt, in);
    CHECK(!pr.ok);
    CHECK(hasStageError(pr, "parse"));
    bool found = false;
    for (const auto& d : pr.diagnostics) {
        if (d.message.find("超过上限") != std::string::npos) {
            found = true;
        }
    }
    CHECK(found);
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    std::string langRoot = "./test/languages";
    if (argc > 1) {
        langRoot = argv[1];
    }

    const std::string tinyBnf = loadFile(langRoot + "/tiny/tiny.bnf");
    const std::string minicBnf = loadFile(langRoot + "/minic/minic.bnf");
    const std::string tinySrc = loadFile(langRoot + "/tiny/samples/test.tiny");
    const std::string minicSrc = loadFile(langRoot + "/minic/samples/test.minic");
    if (tinyBnf.empty() || minicBnf.empty() || tinySrc.empty() || minicSrc.empty()) {
        std::cerr << "FAIL: 找不到语言包（" << langRoot
                  << "/tiny|minic/*.{bnf,rules,samples}）" << std::endl;
        return 1;
    }

    bool ok = true;
    ok &= testTiny(langRoot);
    ok &= testMinic(langRoot);
    ok &= testSingleProduction();
    ok &= testEpsilonOnly();
    ok &= testErrorUnmapped();
    ok &= testErrorActionMissing();
    ok &= testErrorDeleteToken(langRoot);
    ok &= testLoopDefense();

    if (!ok) {
        std::cerr << "FAIL: 存在未通过的 CHECK（已通过 " << g_checks
                  << " 项检查）" << std::endl;
        return 1;
    }
    std::cout << "lr_driver_test OK, checks=" << g_checks << std::endl;
    return 0;
}
