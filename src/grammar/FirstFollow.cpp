// T2.3 First / Follow / Nullable 集合实现（FirstFollow）
// 算法（docs/PROGRESS.md 决策点 / DATA_STRUCTURES §5.2）：
//   阶段顺序 nullable → First → Follow（各自不动点）：
//     nullable : rhs 空 → 可空；X->Y1..Yn 全可空 → X 可空。
//     First    : 终结符/$/# 预置；X->Y1..Yn 逐个加 First(Yi)\{ε}、Yi 可空则继续，
//                全部可空末尾加 ε（kEpsilonSymbolId）。
//     Follow   : follow[start]={$}；A->αBβ 加 First(β)\{ε}，β 可空（含空）再加 Follow(A)。
//   关键正确性点：First(α) 只在 α 整体可空时加入 ε（ε 剥离），否则在 B 可空、C 不可空时
//   FIRST(B C) 会错误含 ε，进而污染 T2.5 lookahead 与 Follow 的「β 可空」判定。

#include "FirstFollow.h"

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
    d.stage = "first-follow";
    d.message = msg;
    d.loc.line = -1;
    d.loc.column = -1;
    d.loc.offset = -1;
    return d;
}

bool isNonterminal(const Symbol* s)
{
    return s && s->kind == SymbolKind::Nonterminal;
}

} // namespace

Result<FirstFollowSets> FirstFollow::compute(const Grammar& g) const
{
    FirstFollowSets sets;

    // ---- 防御校验 ----
    if (g.startSymbol < 0 || !g.symbol(g.startSymbol)) {
        return Result<FirstFollowSets>::failure(makeError("开始符号缺失（startSymbol 无效）"));
    }
    if (g.productions.empty()) {
        return Result<FirstFollowSets>::failure(makeError("文法不含任何产生式"));
    }
    for (const Production& p : g.productions) {
        if (!g.symbol(p.lhs)) {
            return Result<FirstFollowSets>::failure(
                makeError("产生式 #" + std::to_string(p.id) + " 左部符号 id 无效"));
        }
        for (int s : p.rhs) {
            if (!g.symbol(s)) {
                return Result<FirstFollowSets>::failure(
                    makeError("产生式 #" + std::to_string(p.id) + " 右部符号 id 无效"));
            }
        }
    }

    // ---- 预置（按符号 id 索引，长度 = symbols.size()） ----
    const size_t nSym = g.symbols.size();
    sets.first.assign(nSym, {});
    sets.follow.assign(nSym, {});
    sets.nullable.assign(nSym, false);
    for (const Symbol& s : g.symbols) {
        // 终结符、$（EndOfFile）、#（Empty）：First 为自身；# 可空（ε 记号）
        if (s.kind == SymbolKind::Terminal || s.kind == SymbolKind::EndOfFile ||
            s.kind == SymbolKind::Empty) {
            sets.first[static_cast<size_t>(s.id)].insert(s.id);
        }
        if (s.kind == SymbolKind::Empty) {
            sets.nullable[static_cast<size_t>(s.id)] = true;
        }
    }

    // ---- 阶段 1：nullable（不动点） ----
    {
        bool changed;
        do {
            changed = false;
            for (const Production& p : g.productions) {
                const size_t lhs = static_cast<size_t>(p.lhs);
                if (sets.nullable[lhs]) {
                    continue;
                }
                bool allNullable = true;
                for (int s : p.rhs) {
                    if (!sets.nullable[static_cast<size_t>(s)]) {
                        allNullable = false;
                        break;
                    }
                }
                if (allNullable) {
                    sets.nullable[lhs] = true;
                    changed = true;
                }
            }
        } while (changed);
    }

    // ---- 阶段 2：First（不动点，依赖最终 nullable） ----
    {
        bool changed;
        do {
            changed = false;
            for (const Production& p : g.productions) {
                const size_t lhs = static_cast<size_t>(p.lhs);
                const std::set<int> produced = firstOfString(g, sets, p.rhs);
                for (int t : produced) {
                    if (sets.first[lhs].insert(t).second) {
                        changed = true;
                    }
                }
            }
        } while (changed);
    }

    // ---- 阶段 3：Follow（不动点，依赖最终 First + nullable） ----
    sets.follow[static_cast<size_t>(g.startSymbol)].insert(0);   // $（EndOfFile，id 0）
    {
        bool changed;
        do {
            changed = false;
            for (const Production& p : g.productions) {
                const size_t lhs = static_cast<size_t>(p.lhs);
                const std::vector<int>& rhs = p.rhs;
                for (size_t i = 0; i < rhs.size(); ++i) {
                    const Symbol* b = g.symbol(rhs[i]);
                    if (!isNonterminal(b)) {
                        continue;
                    }
                    const size_t bidx = static_cast<size_t>(rhs[i]);
                    // β = rhs[i+1 .. end]
                    std::vector<int> beta;
                    for (size_t j = i + 1; j < rhs.size(); ++j) {
                        beta.push_back(rhs[j]);
                    }
                    const std::set<int> f = firstOfString(g, sets, beta);
                    for (int t : f) {
                        if (t != kEpsilonSymbolId &&
                            sets.follow[bidx].insert(t).second) {
                            changed = true;
                        }
                    }
                    // β 可空（含 β 空）→ Follow(B) ⊇ Follow(A)（链式传播）
                    if (f.count(kEpsilonSymbolId) != 0) {
                        for (int t : sets.follow[lhs]) {
                            if (sets.follow[bidx].insert(t).second) {
                                changed = true;
                            }
                        }
                    }
                }
            }
        } while (changed);
    }

    return Result<FirstFollowSets>::success(std::move(sets));
}

std::set<int> FirstFollow::firstOfString(const Grammar& g, const FirstFollowSets& sets,
                                         const std::vector<int>& symbols)
{
    std::set<int> out;
    bool allNullable = true;
    for (int s : symbols) {
        const Symbol* sym = g.symbol(s);
        if (!sym) {
            // 防御：无效 id 按不可空处理并停止扫描
            allNullable = false;
            break;
        }
        const size_t idx = static_cast<size_t>(s);
        if (idx < sets.first.size()) {
            for (int t : sets.first[idx]) {
                if (t != kEpsilonSymbolId) {
                    out.insert(t);   // 只加非 ε 部分（ε 剥离）
                }
            }
        }
        if (idx >= sets.nullable.size() || !sets.nullable[idx]) {
            allNullable = false;
            break;
        }
    }
    // ε 仅当 α 整体可空时于末尾加入
    if (allNullable) {
        out.insert(kEpsilonSymbolId);
    }
    return out;
}

} // namespace compiler::grammar
