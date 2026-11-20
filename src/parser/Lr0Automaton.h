#pragma once

// LR(0) 项目集族构造（T2.4，依据 docs/PROGRESS.md 决策点 与 docs/DATA_STRUCTURES.md §5.3）
// 纯 C++17 + STL，不依赖 Qt。输入化简后 Grammar（T2.2 产出）→ Result<Lr0AutomatonData>。
//
// 增广文法（决策点 3 / C.3）：物化 S' -> S，仅存在于 Lr0AutomatonData 内部，不写入 Grammar：
//   augmentedStartSymbolId = g.symbols.size()（该 id 不在 g.symbols 内）
//   augmentedProductionId  = g.productions.size()
//   S' 永不出现于任何产生式右部 → closure 的「B 非终结符展开」无需特判 S'。
//
// 算法（closure / goto / BFS）：
//   closure(I) : 对每个 [A->α·Bβ]（B 非终结符）加入 B 的全部产生式 [B->·γ]，迭代到不动点。
//   goto(I, X) : 点前移后对 J 做 closure。
//   初始状态    : closure({ [S'->·S] })。
//   BFS         : 从状态 0 对每个出边符号计算 goto，新状态分配 id（从 0 起、BFS 发现序）。
//
// 不变量：每个状态都是 closure 完备集；所有状态由状态 0 可达；转移目标/符号 id 均合法。
// ε 产生式 rhs={} → 项 [A->·]（dot==rhs.size()）即完成项，无出边、不被 goto 消费。
//
// 失败诊断：stage = "lr0-build"（Severity::Error）：startSymbol 无效 / 产生式为空 /
//           产生式引用不存在的符号 id（防御）。

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Grammar.h"
#include "Result.h"

namespace compiler::parser {

// LR(0) 项：[A -> α · β]，dot ∈ [0, rhs.size()]
struct Item {
    int productionId = -1;
    int dot = 0;

    bool operator<(const Item& o) const
    {
        if (productionId != o.productionId) {
            return productionId < o.productionId;
        }
        return dot < o.dot;
    }
    bool operator==(const Item& o) const
    {
        return productionId == o.productionId && dot == o.dot;
    }
};

// LR(0) 状态 = 一个 closure 完备的项目集
struct Lr0State {
    int id = -1;
    std::set<Item> items;
};

// LR(0) 自动机产物
struct Lr0AutomatonData {
    std::vector<Lr0State> states;                     // id 从 0 起、BFS 发现序
    std::map<std::pair<int, int>, int> transitions;   // (stateId, symbolId) -> stateId

    // 增广文法 S' -> S（仅存在于自动机内部，不写入 Grammar）
    int              augmentedStartSymbolId = -1;     // = g.symbols.size()
    std::string      augmentedStartSymbolName = "S'"; // GUI 显示用
    int              augmentedProductionId = -1;      // = g.productions.size()
    std::vector<int> augmentedProductionRhs;          // = {g.startSymbol}
};

class Lr0Automaton {
public:
    // 构建 LR(0) 项目集族。
    Result<Lr0AutomatonData> build(const Grammar& g) const;
};

} // namespace compiler::parser
