// T2.5 LALR(1) 项目集族实现（Lalr1Builder，路线 B：LR(1) + 同心合并）
// 算法（docs/PROGRESS.md 决策点 1 / DATA_STRUCTURES §5.4）：
//   1) 防御校验（Grammar / FirstFollowSets / Lr0AutomatonData 一致性）。
//   2) LR(1) closure：对 [A -> α·Bβ, a]（B 非终结符），lookahead b ∈ FIRST(β a)，
//      FIRST(β a) = FirstFollow::firstOfString(g, sets, β ++ [a])（不手写 First 传播）。
//   3) LR(1) goto / BFS：状态 key = std::set<Lr1Item>，从 closure({[S'->·S, $]}) 出发。
//   4) 同心合并：LR(0) 核心索引（std::set<Item> → LR(0) 状态 id），每个 LR(1) 状态
//      投影核心查索引，lookahead 并入对应合并态；合并态 id 复用 LR(0) id，转移按核心换算。
//   5) 返回 Lalr1Data（含 intermediateLr1* 中间产物）。
// 关键性质（测试断言）：合并只可能新增 reduce/reduce，绝不新增 shift/reduce。

#include "Lalr1Builder.h"

#include <queue>
#include <string>
#include <utility>

namespace compiler::parser {
namespace {

Diagnostic makeError(const std::string& msg)
{
    Diagnostic d;
    d.severity = Severity::Error;
    d.stage = "lalr1-build";
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

Result<Lalr1Data> Lalr1Builder::build(const Grammar& g, const grammar::FirstFollowSets& sets,
                                      const Lr0AutomatonData& lr0) const
{
    Lalr1Data data;

    // ---- 防御校验 ----
    if (g.startSymbol < 0 || !g.symbol(g.startSymbol)) {
        return Result<Lalr1Data>::failure(makeError("开始符号缺失（startSymbol 无效）"));
    }
    if (g.productions.empty()) {
        return Result<Lalr1Data>::failure(makeError("文法不含任何产生式"));
    }
    if (lr0.states.empty()) {
        return Result<Lalr1Data>::failure(
            makeError("LR(0) 自动机为空（Lr0AutomatonData.states 为空）"));
    }
    if (lr0.augmentedProductionId != static_cast<int>(g.productions.size()) ||
        lr0.augmentedProductionRhs.size() != 1 ||
        lr0.augmentedProductionRhs[0] != g.startSymbol) {
        return Result<Lalr1Data>::failure(
            makeError("LR(0) 增广文法信息与 Grammar 不一致"));
    }
    if (sets.first.size() != g.symbols.size() || sets.nullable.size() != g.symbols.size()) {
        return Result<Lalr1Data>::failure(makeError("FirstFollowSets 尺寸与 Grammar 不符"));
    }
    for (const Production& p : g.productions) {
        if (!g.symbol(p.lhs)) {
            return Result<Lalr1Data>::failure(
                makeError("产生式 #" + std::to_string(p.id) + " 左部符号 id 无效"));
        }
        for (int s : p.rhs) {
            if (!g.symbol(s)) {
                return Result<Lalr1Data>::failure(
                    makeError("产生式 #" + std::to_string(p.id) + " 右部符号 id 无效"));
            }
        }
    }

    // ---- 增广信息透传 ----
    data.augmentedStartSymbolId = lr0.augmentedStartSymbolId;
    data.augmentedStartSymbolName = lr0.augmentedStartSymbolName;
    data.augmentedProductionId = lr0.augmentedProductionId;
    data.augmentedProductionRhs = lr0.augmentedProductionRhs;

    // 产生式右部访问路由：增广 id → 增广产生式，否则 → g.productions
    const auto rhsOf = [&](int pid) -> const std::vector<int>& {
        if (pid == data.augmentedProductionId) {
            return data.augmentedProductionRhs;
        }
        return g.productions[static_cast<size_t>(pid)].rhs;
    };

    // closure：对 [A -> α·Bβ, a]，引入 [B -> ·γ, b]，b ∈ FIRST(β a)（firstOfString，不手写）
    const auto closure = [&](std::set<Lr1Item> items) -> std::set<Lr1Item> {
        std::vector<Lr1Item> stack(items.begin(), items.end());
        while (!stack.empty()) {
            const Lr1Item it = stack.back();
            stack.pop_back();
            const std::vector<int>& r = rhsOf(it.productionId);
            if (it.dot < 0 || static_cast<size_t>(it.dot) >= r.size()) {
                continue;   // 完成项（dot==rhs.size()）：无出边
            }
            const int B = r[static_cast<size_t>(it.dot)];
            const Symbol* sym = g.symbol(B);
            if (!isNonterminal(sym)) {
                continue;
            }
            // β ++ [a]，FIRST(β a) 因 a 为终结符天然不含 ε；仍防御剥离 kEpsilonSymbolId
            std::vector<int> betaAndA;
            for (size_t k = static_cast<size_t>(it.dot) + 1; k < r.size(); ++k) {
                betaAndA.push_back(r[k]);
            }
            betaAndA.push_back(it.lookahead);
            const std::set<int> f = grammar::FirstFollow::firstOfString(g, sets, betaAndA);
            for (int pid : g.productionsOf(B)) {
                for (int b : f) {
                    if (b == grammar::kEpsilonSymbolId) {
                        continue;
                    }
                    const Lr1Item added{ pid, 0, b };
                    if (items.insert(added).second) {
                        stack.push_back(added);
                    }
                }
            }
        }
        return items;
    };

    // goto(I, X)：点前移 + closure
    const auto gotoSet = [&](const std::set<Lr1Item>& items, int X) -> std::set<Lr1Item> {
        std::set<Lr1Item> j;
        for (const Lr1Item& it : items) {
            const std::vector<int>& r = rhsOf(it.productionId);
            if (it.dot >= 0 && static_cast<size_t>(it.dot) < r.size() &&
                r[static_cast<size_t>(it.dot)] == X) {
                j.insert({ it.productionId, it.dot + 1, it.lookahead });
            }
        }
        return closure(std::move(j));
    };

    // ---- 规范 LR(1) BFS ----
    std::set<Lr1Item> init{ Lr1Item{ data.augmentedProductionId, 0, 0 } };  // [S' -> ·S, $]
    const std::set<Lr1Item> state0 = closure(std::move(init));

    std::map<std::set<Lr1Item>, int> stateIndex;
    stateIndex[state0] = 0;
    data.intermediateLr1States.push_back(Lr1StateRaw{ 0, state0 });

    std::queue<int> pending;
    pending.push(0);

    while (!pending.empty()) {
        const int sid = pending.front();
        pending.pop();
        // 拷贝状态项目集：后续 push_back 会使 intermediateLr1States 重新分配，引用会悬垂
        const std::set<Lr1Item> st = data.intermediateLr1States[static_cast<size_t>(sid)].items;

        // 出边符号 = 所有项点后符号的集合
        std::set<int> outSyms;
        for (const Lr1Item& it : st) {
            const std::vector<int>& r = rhsOf(it.productionId);
            if (it.dot >= 0 && static_cast<size_t>(it.dot) < r.size()) {
                outSyms.insert(r[static_cast<size_t>(it.dot)]);
            }
        }

        for (int X : outSyms) {
            const std::set<Lr1Item> tgt = gotoSet(st, X);
            if (tgt.empty()) {
                continue;
            }
            int tid;
            const auto found = stateIndex.find(tgt);
            if (found == stateIndex.end()) {
                tid = static_cast<int>(data.intermediateLr1States.size());
                stateIndex[tgt] = tid;
                data.intermediateLr1States.push_back(Lr1StateRaw{ tid, tgt });
                pending.push(tid);
            } else {
                tid = found->second;
            }
            data.intermediateLr1Transitions[{sid, X}] = tid;
        }
    }

    // ---- 同心合并 ----
    // LR(0) 核心索引：std::set<Item>（(productionId, dot) 集合）→ LR(0) 状态 id
    std::map<std::set<Item>, int> coreToLr0;
    for (const Lr0State& st : lr0.states) {
        coreToLr0[st.items] = st.id;
    }

    std::vector<int> coreOfLr1(data.intermediateLr1States.size(), -1);
    std::vector<std::map<std::pair<int, int>, std::set<int>>> mergedLookaheads(
        lr0.states.size());
    for (const Lr1StateRaw& st : data.intermediateLr1States) {
        std::set<Item> core;
        for (const Lr1Item& it : st.items) {
            core.insert(Item{ it.productionId, it.dot });
        }
        const auto found = coreToLr0.find(core);
        if (found == coreToLr0.end()) {
            return Result<Lalr1Data>::failure(
                makeError("内部不一致：LR(1) 状态核心在 LR(0) 索引中不存在"));
        }
        const int cid = found->second;
        coreOfLr1[static_cast<size_t>(st.id)] = cid;
        for (const Lr1Item& it : st.items) {
            mergedLookaheads[static_cast<size_t>(cid)][{ it.productionId, it.dot }]
                .insert(it.lookahead);
        }
    }

    // 按 LR(0) id 序重建合并态
    for (size_t cid = 0; cid < lr0.states.size(); ++cid) {
        Lalr1State st;
        st.id = static_cast<int>(cid);
        for (const auto& kv : mergedLookaheads[cid]) {
            st.items.insert(Lalr1Item{ kv.first.first, kv.first.second, kv.second });
        }
        data.states.push_back(std::move(st));
    }

    // 转移换算：(core(s1), X) -> core(s2)
    for (const auto& kv : data.intermediateLr1Transitions) {
        const int s = coreOfLr1[static_cast<size_t>(kv.first.first)];
        const int t = coreOfLr1[static_cast<size_t>(kv.second)];
        data.transitions[{ s, kv.first.second }] = t;
    }

    return Result<Lalr1Data>::success(std::move(data));
}

} // namespace compiler::parser
