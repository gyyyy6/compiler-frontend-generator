#pragma once

// LALR(1) 项目集族构造（T2.5，依据 docs/PROGRESS.md 决策点 1 路线 B 与 docs/DATA_STRUCTURES.md §5.4）
// 纯 C++17 + STL，不依赖 Qt。
// 输入：化简后 Grammar（T2.2 产出）+ FirstFollowSets（T2.3 产出）+ Lr0AutomatonData（T2.4 产出）
//       → Result<Lalr1Data>。
//
// 路线 B：先构造规范 LR(1) 项目集族，再按 LR(0) 核心同心合并（lookahead 取并集）。
//   - LR(1) closure：对 [A -> α·Bβ, a]（B 非终结符），lookahead b ∈ FIRST(β a)，
//     FIRST(β a) = FirstFollow::firstOfString(g, sets, β ++ [a])（不手写 First 传播）。
//   - 初始状态：closure({ [S' -> ·S, $] })；BFS 去重 key = std::set<Lr1Item>。
//   - 合并后状态 id 复用 LR(0) 状态 id（states.size() == lr0.states.size()），
//     合并转移 == LR(0) 转移（同心核心 → goto 结构相同）。
//   - 关键性质（测试断言）：合并不引入新的 shift/reduce 冲突（只可能新增 reduce/reduce）。
//
// 增广文法（透传自 Lr0AutomatonData，不写入 Grammar）：S' -> S，
//   augmentedStartSymbolId = lr0.augmentedStartSymbolId、augmentedProductionId = lr0.augmentedProductionId；
//   产生式右部访问统一走 rhsOf（pid == augmentedProductionId ? augmentedRhs : g.productions[pid].rhs）。
// ε 产生式 rhs={} → 项 [A->·]（dot==rhs.size()）即完成项，无出边、不被 goto 消费。
//
// 失败诊断：stage = "lalr1-build"（Severity::Error）：
//   - startSymbol 无效 / 产生式为空 / 产生式引用不存在的符号 id（防御）
//   - Lr0AutomatonData 为空 / 增广信息与 Grammar 不一致 / FirstFollowSets 尺寸与 Grammar 不符（防御）
//   - LR(1) 状态核心在 LR(0) 索引中找不到（内部不一致）

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "FirstFollow.h"
#include "Grammar.h"
#include "Lr0Automaton.h"
#include "Result.h"

namespace compiler::parser {

// LR(1) 项：单终结符 lookahead（LR(1) 中间构造用；合并后不再使用）
struct Lr1Item {
    int productionId = -1;
    int dot = 0;
    int lookahead = -1;   // 单终结符 id（$=0）

    bool operator<(const Lr1Item& o) const
    {
        if (productionId != o.productionId) {
            return productionId < o.productionId;
        }
        if (dot != o.dot) {
            return dot < o.dot;
        }
        return lookahead < o.lookahead;
    }
};

// 规范 LR(1) 状态（合并前，中间产物）
struct Lr1StateRaw {
    int id = -1;
    std::set<Lr1Item> items;
};

// 合并后的 LALR(1) 项：lookahead 为集合
struct Lalr1Item {
    int productionId = -1;
    int dot = 0;
    std::set<int> lookaheads;   // 终结符 id 集合（含 $=0，不含 ε）

    bool operator<(const Lalr1Item& o) const
    {
        if (productionId != o.productionId) {
            return productionId < o.productionId;
        }
        if (dot != o.dot) {
            return dot < o.dot;
        }
        return lookaheads < o.lookaheads;
    }
};

// 合并后的 LALR(1) 状态
struct Lalr1State {
    int id = -1;
    std::set<Lalr1Item> items;
};

// LALR(1) 自动机产物
struct Lalr1Data {
    std::vector<Lalr1State> states;                    // 合并后状态，id == 对应 LR(0) 状态 id
    std::map<std::pair<int, int>, int> transitions;    // (stateId, symbolId) -> stateId

    // 规范 LR(1) 中间产物（测试「不引入新冲突」/调试用，公开成员）
    std::vector<Lr1StateRaw> intermediateLr1States;
    std::map<std::pair<int, int>, int> intermediateLr1Transitions;

    // 增广文法 S' -> S（透传自 Lr0AutomatonData，不写入 Grammar）
    int              augmentedStartSymbolId = -1;      // = g.symbols.size()
    std::string      augmentedStartSymbolName = "S'";  // GUI 显示用
    int              augmentedProductionId = -1;       // = g.productions.size()
    std::vector<int> augmentedProductionRhs;           // = {g.startSymbol}
};

class Lalr1Builder {
public:
    // 构建 LALR(1) 项目集族（路线 B：LR(1) + 同心合并）。
    Result<Lalr1Data> build(const Grammar& g, const grammar::FirstFollowSets& sets,
                            const Lr0AutomatonData& lr0) const;
};

} // namespace compiler::parser
