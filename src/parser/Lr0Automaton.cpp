// T2.4 LR(0) 项目集族实现（Lr0Automaton）
// 算法（docs/PROGRESS.md 决策点 / DATA_STRUCTURES §5.3）：
//   1) 物化增广产生式 S' -> S（id = g.productions.size()），S' 符号 id = g.symbols.size()。
//   2) closure(I)：对点后为「非终结符 B」的项，加入 B 的全部产生式 dot=0 项，栈迭代到不动点。
//   3) goto(I,X)：点前移 + closure。
//   4) BFS 从 closure({[S'->·S]}) 出发，std::set<Item> 去重，transitions 记录。
// 关键细节：
//   - ε 产生式 rhs={} → 项 [A->·]（dot==rhs.size()）即完成项，无出边、不被 goto 消费。
//   - 所有产生式访问统一走 rhsOf（含增广产生式），杜绝越界。
//   - 状态 id 从 0 起 = BFS 发现序。

#include "Lr0Automaton.h"

#include <queue>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace compiler::parser {
namespace {

Diagnostic makeError(const std::string& msg)
{
    Diagnostic d;
    d.severity = Severity::Error;
    d.stage = "lr0-build";
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

Result<Lr0AutomatonData> Lr0Automaton::build(const Grammar& g) const
{
    Lr0AutomatonData data;

    // ---- 防御校验 ----
    if (g.startSymbol < 0 || !g.symbol(g.startSymbol)) {
        return Result<Lr0AutomatonData>::failure(makeError("开始符号缺失（startSymbol 无效）"));
    }
    if (g.productions.empty()) {
        return Result<Lr0AutomatonData>::failure(makeError("文法不含任何产生式"));
    }
    for (const Production& p : g.productions) {
        if (!g.symbol(p.lhs)) {
            return Result<Lr0AutomatonData>::failure(
                makeError("产生式 #" + std::to_string(p.id) + " 左部符号 id 无效"));
        }
        for (int s : p.rhs) {
            if (!g.symbol(s)) {
                return Result<Lr0AutomatonData>::failure(
                    makeError("产生式 #" + std::to_string(p.id) + " 右部符号 id 无效"));
            }
        }
    }

    // ---- 增广文法 S' -> S（仅内部，不写入 Grammar） ----
    data.augmentedStartSymbolId = static_cast<int>(g.symbols.size());
    data.augmentedStartSymbolName = "S'";
    data.augmentedProductionId = static_cast<int>(g.productions.size());
    data.augmentedProductionRhs = { g.startSymbol };

    // 产生式右部访问路由：增广 id → 增广产生式，否则 → g.productions
    const auto rhsOf = [&](int pid) -> const std::vector<int>& {
        if (pid == data.augmentedProductionId) {
            return data.augmentedProductionRhs;
        }
        return g.productions[static_cast<size_t>(pid)].rhs;
    };

    // closure：栈迭代，返回闭包完备集
    const auto closure = [&](std::set<Item> items) -> std::set<Item> {
        std::vector<Item> stack(items.begin(), items.end());
        while (!stack.empty()) {
            const Item it = stack.back();
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
            for (int pid : g.productionsOf(B)) {
                const Item added{ pid, 0 };
                if (items.insert(added).second) {
                    stack.push_back(added);
                }
            }
        }
        return items;
    };

    // goto(I, X)：点前移 + closure
    const auto gotoSet = [&](const std::set<Item>& items, int X) -> std::set<Item> {
        std::set<Item> j;
        for (const Item& it : items) {
            const std::vector<int>& r = rhsOf(it.productionId);
            if (it.dot >= 0 && static_cast<size_t>(it.dot) < r.size() &&
                r[static_cast<size_t>(it.dot)] == X) {
                j.insert({ it.productionId, it.dot + 1 });
            }
        }
        return closure(std::move(j));
    };

    // ---- BFS ----
    std::set<Item> init{ Item{ data.augmentedProductionId, 0 } };
    const std::set<Item> state0 = closure(std::move(init));

    std::map<std::set<Item>, int> stateIndex;
    stateIndex[state0] = 0;
    data.states.push_back(Lr0State{ 0, state0 });

    std::queue<int> pending;
    pending.push(0);

    while (!pending.empty()) {
        const int sid = pending.front();
        pending.pop();
        // 拷贝状态项目集：后续 push_back 会使 data.states 重新分配，引用会悬垂
        const std::set<Item> st = data.states[static_cast<size_t>(sid)].items;

        // 出边符号 = 所有项点后符号的集合
        std::set<int> outSyms;
        for (const Item& it : st) {
            const std::vector<int>& r = rhsOf(it.productionId);
            if (it.dot >= 0 && static_cast<size_t>(it.dot) < r.size()) {
                outSyms.insert(r[static_cast<size_t>(it.dot)]);
            }
        }

        for (int X : outSyms) {
            const std::set<Item> tgt = gotoSet(st, X);
            if (tgt.empty()) {
                continue;
            }
            int tid;
            const auto found = stateIndex.find(tgt);
            if (found == stateIndex.end()) {
                tid = static_cast<int>(data.states.size());
                stateIndex[tgt] = tid;
                data.states.push_back(Lr0State{ tid, tgt });
                pending.push(tid);
            } else {
                tid = found->second;
            }
            data.transitions[{sid, X}] = tid;
        }
    }

    return Result<Lr0AutomatonData>::success(std::move(data));
}

} // namespace compiler::parser
