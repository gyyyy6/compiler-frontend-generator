#pragma once

// LALR(1) 分析表 ACTION/GOTO 构造（T2.6，依据 docs/PROGRESS.md 决策点 6 与 docs/DATA_STRUCTURES.md §5.5）
// 纯 C++17 + STL，不依赖 Qt。
// 输入：化简后 Grammar（T2.2 产出）+ Lalr1Data（T2.5 产出）→ Result<ParseTableData>。
//
// 填表规则（DATA_STRUCTURES §5.5，单趟扫描）：
//   - GOTO ：transitions 中 (I, A) 且 A 非终结符 → gotoTable[I][A] = J。
//   - Shift：(I, a) 且 a 终结符 → action[I][a] = Shift(J)（对应 [A -> α·aβ] 且 goto(I,a)=J）。
//   - Reduce：完成项 [A -> α·]（A ≠ S'）对每个 b ∈ lookaheads → action[I][b] = Reduce(pid)。
//   - Accept：完成项 [S' -> S·] 且 lookahead 含 $ → action[I][0] = Accept。
//
// 冲突检测（决策点 6：一律报告，不静默解决）：统一 writeAction，同格已被占用即冲突。
//   - S/R：已有 Shift 后填 Reduce → 记录 "shift/reduce"，表内保留 Shift（标准做法）。
//   - R/R：已有 Reduce(p1) 再填 Reduce(p2)（p1≠p2）→ 记录 "reduce/reduce"，表内保留先填（低产生式 id）。
//   - 同一 (pid,dot) 的 lookahead 已由 T2.5 合并为集合 → 同格同 p 幂等跳过，不计冲突。
//   - 冲突不影响 Result.ok（冲突是「报告」而非「失败」）。
//
// Action.target 语义：Shift = 目标状态 id；Reduce = 产生式 id；Accept = -1（忽略）；Error = -1。
//
// 失败诊断：stage = "parse-table"（Severity::Error）：
//   - startSymbol 无效 / 产生式为空 / Lalr1Data 为空 / 增广信息与 Grammar 不一致 / 转移引用非法（防御）。

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "Grammar.h"
#include "Lalr1Builder.h"
#include "Result.h"

namespace compiler::parser {

// 动作类型：Error 为缺省（表中无该格即 Error，map 不显式存储）
enum class ActionType : int { Shift, Reduce, Accept, Error };

// 单个动作
struct Action {
    ActionType type = ActionType::Error;
    int target = -1;   // Shift: 目标状态 id；Reduce: 产生式 id；Accept/Error: -1（忽略）
};

// 冲突记录（决策点 6：一律报告，不静默解决）
struct Conflict {
    int state = -1;                 // 冲突所在状态 id
    int terminal = -1;              // 终结符 id（$ = 0）
    std::string type;               // "shift/reduce" | "reduce/reduce"
    std::vector<int> productions;   // 涉及的归约产生式 id（S/R 仅 1 个；R/R 两个按 id 升序）
};

// LALR(1) 分析表产物
struct ParseTableData {
    std::vector<std::map<int, Action>> action;      // [state][terminalId]（含 $=0）；缺省 Error
    std::vector<std::map<int, int>>    gotoTable;   // [state][nonterminalId]
    std::vector<Conflict> conflicts;                // 按遍历序稳定

    // 增广文法信息（透传自 Lalr1Data，T2.8 驱动 / GUI 显示复用）
    int              augmentedStartSymbolId = -1;
    std::string      augmentedStartSymbolName = "S'";
    int              augmentedProductionId = -1;
    std::vector<int> augmentedProductionRhs;
};

class ParseTableBuilder {
public:
    // 构建 LALR(1) 分析表（ACTION/GOTO）+ 冲突报告。
    Result<ParseTableData> build(const Grammar& g, const Lalr1Data& lalr) const;
};

} // namespace compiler::parser
