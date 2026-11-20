// T2.2 文法化简实现（GrammarSimplifier，决策点 3）
// 算法：
//   阶段 1（无用）：
//     - productive 不动点：右部全为"终结符 或 productive 非终结符" → 左部 productive；
//       ε 产生式（rhs 为空）直接使左部 productive。
//     - 删除条件：左部非 productive **或** 右部含非 productive 非终结符。
//       外层迭代到稳定：删除后若某左部失去全部产生式，下一轮 productive 重算会把它
//       暴露为非 productive，并连锁删除其产生式（保证不留下悬垂引用，见测试 #7）。
//   阶段 2（不可达）：在阶段 1 存活产生式上，从 startSymbol BFS 标记可达符号，
//     删除不可达符号及其产生式。
//   重建：符号/产生式 id 稠密重编号，保留 $ = id 0、# = id 1；日志记原始 id。

#include "GrammarSimplifier.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace compiler::grammar {
namespace {

Diagnostic makeError(const std::string& msg)
{
    Diagnostic d;
    d.severity = Severity::Error;
    d.stage = "simplify";
    d.message = msg;
    d.loc.line = -1;
    d.loc.column = -1;
    d.loc.offset = -1;
    return d;
}

std::string symbolName(const Grammar& g, int id)
{
    const Symbol* s = g.symbol(id);
    return s ? s->name : "<?>";
}

// 在 alive 产生式上计算 productive 不动点
void computeProductive(const Grammar& g, const std::vector<bool>& alive,
                       std::set<int>& productive)
{
    productive.clear();
    bool changed;
    do {
        changed = false;
        for (size_t i = 0; i < g.productions.size(); ++i) {
            if (!alive[i]) {
                continue;
            }
            const Production& p = g.productions[i];
            if (productive.count(p.lhs) != 0) {
                continue;
            }
            bool allOk = true;
            for (int s : p.rhs) {
                const Symbol* sym = g.symbol(s);
                // 防御：悬垂 id（BnfParser 不会产生）按不可终结处理
                if (!sym || (sym->kind == SymbolKind::Nonterminal &&
                             productive.count(s) == 0)) {
                    allOk = false;
                    break;
                }
            }
            if (allOk) {
                productive.insert(p.lhs);
                changed = true;
            }
        }
    } while (changed);
}

// 在 alive 产生式上从 startSymbol BFS 标记可达符号
void computeReachable(const Grammar& g, const std::vector<bool>& alive,
                      int startSymbol, std::set<int>& reachable)
{
    reachable.clear();
    if (startSymbol < 0) {
        return;
    }
    std::vector<int> stack{ startSymbol };
    reachable.insert(startSymbol);
    while (!stack.empty()) {
        const int cur = stack.back();
        stack.pop_back();
        for (size_t i = 0; i < g.productions.size(); ++i) {
            if (!alive[i]) {
                continue;
            }
            const Production& p = g.productions[i];
            if (p.lhs != cur) {
                continue;
            }
            for (int s : p.rhs) {
                const Symbol* sym = g.symbol(s);
                if (!sym) {
                    continue;
                }
                // 仅扩散非终结符/终结符；内部符号 $ / # 不作为可达扩散源
                if ((sym->kind == SymbolKind::Nonterminal ||
                     sym->kind == SymbolKind::Terminal) &&
                    reachable.count(s) == 0) {
                    reachable.insert(s);
                    stack.push_back(s);
                }
            }
        }
    }
}

} // namespace

Result<SimplifiedGrammar> GrammarSimplifier::simplify(const Grammar& g) const
{
    SimplifiedGrammar out;
    SimplifyLog& log = out.log;

    // ---- 防御校验 ----
    if (g.startSymbol < 0 || !g.symbol(g.startSymbol)) {
        return Result<SimplifiedGrammar>::failure(makeError("开始符号缺失（startSymbol 无效）"));
    }
    for (const Production& p : g.productions) {
        if (!g.symbol(p.lhs)) {
            return Result<SimplifiedGrammar>::failure(
                makeError("产生式 #" + std::to_string(p.id) + " 左部符号 id 无效"));
        }
        for (int s : p.rhs) {
            if (!g.symbol(s)) {
                return Result<SimplifiedGrammar>::failure(
                    makeError("产生式 #" + std::to_string(p.id) + " 右部符号 id 无效"));
            }
        }
    }

    const size_t nProd = g.productions.size();
    std::vector<bool> alive(nProd, true);

    // ---- 阶段 1：删除无用符号/产生式 ----
    std::set<int> productive;
    std::set<int> removedProdUseless;    // 原始 id
    bool changed = true;
    while (changed) {
        changed = false;
        computeProductive(g, alive, productive);

        // 删除条件：左部非 productive **或** 右部含非 productive 非终结符
        for (size_t i = 0; i < nProd; ++i) {
            if (!alive[i]) {
                continue;
            }
            const Production& p = g.productions[i];
            bool bad = productive.count(p.lhs) == 0;
            if (!bad) {
                for (int s : p.rhs) {
                    const Symbol* sym = g.symbol(s);
                    if (sym && sym->kind == SymbolKind::Nonterminal &&
                        productive.count(s) == 0) {
                        bad = true;
                        break;
                    }
                }
            }
            if (bad) {
                alive[i] = false;
                removedProdUseless.insert(static_cast<int>(i));
                changed = true;
            }
        }
    }

    // startSymbol 不可终结 → 化简后文法为空
    if (productive.count(g.startSymbol) == 0) {
        return Result<SimplifiedGrammar>::failure(
            makeError("开始符号 '" + symbolName(g, g.startSymbol) +
                      "' 不可导出终结符串，化简后文法为空"));
    }

    // 阶段 1 被删符号：非 productive 的非终结符（reason=useless）
    std::set<int> removedSymUseless;
    for (const Symbol& s : g.symbols) {
        if (s.kind == SymbolKind::Nonterminal && productive.count(s.id) == 0) {
            removedSymUseless.insert(s.id);
        }
    }

    // ---- 阶段 2：删除不可达符号/产生式 ----
    std::set<int> reachable;
    computeReachable(g, alive, g.startSymbol, reachable);

    std::set<int> removedProdUnreachable;
    for (size_t i = 0; i < nProd; ++i) {
        if (alive[i] && reachable.count(g.productions[i].lhs) == 0) {
            alive[i] = false;
            removedProdUnreachable.insert(static_cast<int>(i));
        }
    }

    // 阶段 2 被删符号：不可达的用户符号（非终结符 + 终结符）
    std::set<int> removedSymUnreachable;
    for (const Symbol& s : g.symbols) {
        if ((s.kind == SymbolKind::Nonterminal || s.kind == SymbolKind::Terminal) &&
            reachable.count(s.id) == 0 && removedSymUseless.count(s.id) == 0) {
            removedSymUnreachable.insert(s.id);
        }
    }

    // ---- 重建：id 稠密重编号，保留 $ / # 固定槽 ----
    std::vector<int> symMap(g.symbols.size(), -1);

    // $ (EndOfFile, id 0) 与 # (Empty, id 1) 固定保留
    symMap[0] = static_cast<int>(out.grammar.symbols.size());
    out.grammar.symbols.push_back(g.symbols[0]);
    if (g.symbols.size() > 1) {
        symMap[1] = static_cast<int>(out.grammar.symbols.size());
        out.grammar.symbols.push_back(g.symbols[1]);
    }

    // 存活用户符号按原相对顺序（可终结且可达）
    for (size_t i = 2; i < g.symbols.size(); ++i) {
        const Symbol& s = g.symbols[i];
        if (removedSymUseless.count(s.id) == 0 &&
            removedSymUnreachable.count(s.id) == 0) {
            symMap[i] = static_cast<int>(out.grammar.symbols.size());
            Symbol copy = s;
            copy.id = symMap[i];
            out.grammar.symbols.push_back(std::move(copy));
        }
    }

    for (size_t i = 0; i < nProd; ++i) {
        if (!alive[i]) {
            continue;
        }
        Production copy = g.productions[i];
        copy.id = static_cast<int>(out.grammar.productions.size());
        copy.lhs = symMap[copy.lhs];
        for (int& s : copy.rhs) {
            s = symMap[s];
        }
        out.grammar.productions.push_back(std::move(copy));
    }

    out.grammar.startSymbol = symMap[g.startSymbol];

    // ---- 日志：useless 先、unreachable 后，各自按原始 id 升序 ----
    for (int id : removedProdUseless) {
        log.removedProductions.push_back({id, "useless"});
    }
    for (int id : removedProdUnreachable) {
        log.removedProductions.push_back({id, "unreachable"});
    }
    for (int id : removedSymUseless) {
        log.removedSymbols.push_back({g.symbols[static_cast<size_t>(id)].name, "useless"});
    }
    for (int id : removedSymUnreachable) {
        if (g.symbols[static_cast<size_t>(id)].kind == SymbolKind::Nonterminal) {
            log.removedSymbols.push_back({g.symbols[static_cast<size_t>(id)].name, "unreachable"});
        }
    }

    // ---- 摘要 ----
    if (log.empty()) {
        log.summary = "文法已是最简（未删除任何符号或产生式）";
    } else {
        log.summary = "删除无用符号 " + std::to_string(removedSymUseless.size()) +
                      " 个、不可达符号 " + std::to_string(removedSymUnreachable.size()) +
                      " 个，删除产生式 " + std::to_string(log.removedProductions.size()) +
                      " 条";
    }

    return Result<SimplifiedGrammar>::success(std::move(out));
}

} // namespace compiler::grammar
