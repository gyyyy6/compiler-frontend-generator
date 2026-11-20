// T2.6 LALR(1) 分析表 ACTION/GOTO + 冲突报告（ParseTableBuilder）
// 算法（docs/PROGRESS.md 决策点 6 / DATA_STRUCTURES §5.5）：
//   1) 防御校验（Grammar / Lalr1Data 一致性）。
//   2) 单趟扫描：先填 GOTO（非终结符转移）与 Shift（终结符转移），再按完成项填 Reduce/Accept。
//   3) 统一 writeAction：同格已被占用即冲突（决策点 6，一律报告、不静默解决）；
//      S/R 保留 Shift（标准做法）、R/R 保留先填（低产生式 id）Reduce（确定性）。
//   4) 返回 ParseTableData（含增广信息透传，供 T2.8 驱动 / GUI 复用）。

#include "ParseTable.h"

#include <string>
#include <utility>

namespace compiler::parser {
namespace {

Diagnostic makeError(const std::string& msg)
{
    Diagnostic d;
    d.severity = Severity::Error;
    d.stage = "parse-table";
    d.message = msg;
    d.loc.line = -1;
    d.loc.column = -1;
    d.loc.offset = -1;
    return d;
}

} // namespace

Result<ParseTableData> ParseTableBuilder::build(const Grammar& g, const Lalr1Data& lalr) const
{
    ParseTableData data;

    // ---- 防御校验 ----
    if (g.startSymbol < 0 || !g.symbol(g.startSymbol)) {
        return Result<ParseTableData>::failure(makeError("开始符号缺失（startSymbol 无效）"));
    }
    if (g.productions.empty()) {
        return Result<ParseTableData>::failure(makeError("文法不含任何产生式"));
    }
    if (lalr.states.empty()) {
        return Result<ParseTableData>::failure(makeError("LALR(1) 自动机为空（Lalr1Data.states 为空）"));
    }
    if (lalr.augmentedProductionId != static_cast<int>(g.productions.size()) ||
        lalr.augmentedProductionRhs.size() != 1 ||
        lalr.augmentedProductionRhs[0] != g.startSymbol) {
        return Result<ParseTableData>::failure(makeError("LALR(1) 增广文法信息与 Grammar 不一致"));
    }

    // ---- 增广信息透传 ----
    data.augmentedStartSymbolId = lalr.augmentedStartSymbolId;
    data.augmentedStartSymbolName = lalr.augmentedStartSymbolName;
    data.augmentedProductionId = lalr.augmentedProductionId;
    data.augmentedProductionRhs = lalr.augmentedProductionRhs;

    // 产生式右部访问路由：增广 id → 增广产生式，否则 → g.productions
    const auto rhsOf = [&](int pid) -> const std::vector<int>& {
        if (pid == data.augmentedProductionId) {
            return data.augmentedProductionRhs;
        }
        return g.productions[static_cast<size_t>(pid)].rhs;
    };

    const size_t N = lalr.states.size();
    data.action.assign(N, {});
    data.gotoTable.assign(N, {});

    // ---- GOTO（非终结符转移）+ Shift（终结符转移） ----
    for (const auto& kv : lalr.transitions) {
        const int s = kv.first.first;
        const int sym = kv.first.second;
        const int j = kv.second;
        if (s < 0 || static_cast<size_t>(s) >= N) {
            return Result<ParseTableData>::failure(makeError("内部不一致：转移源状态 id 越界"));
        }
        const Symbol* sy = g.symbol(sym);
        if (!sy) {
            return Result<ParseTableData>::failure(makeError("内部不一致：转移符号 id 无效"));
        }
        if (sy->kind == SymbolKind::Nonterminal) {
            data.gotoTable[static_cast<size_t>(s)][sym] = j;
        } else if (sy->kind == SymbolKind::Terminal) {
            // transitions 为确定性 map，(s, sym) 唯一 → 不会出现 shift-shift
            data.action[static_cast<size_t>(s)][sym] = Action{ ActionType::Shift, j };
        }
    }

    // ---- Reduce / Accept（完成项） ----
    // writeAction：同格已被占用即冲突（决策点 6，一律报告、不静默解决）
    const auto writeReduce = [&](int s, int term, int pid) {
        Action& cell = data.action[static_cast<size_t>(s)][term];
        if (cell.type == ActionType::Error) {
            cell = Action{ ActionType::Reduce, pid };
            return;
        }
        if (cell.type == ActionType::Shift) {
            // S/R：记录冲突，表内保留 Shift（标准做法）
            Conflict c;
            c.state = s;
            c.terminal = term;
            c.type = "shift/reduce";
            c.productions.push_back(pid);
            data.conflicts.push_back(std::move(c));
        } else if (cell.type == ActionType::Reduce) {
            if (cell.target != pid) {
                // R/R：记录冲突，表内保留先填（低产生式 id）Reduce（确定性）
                Conflict c;
                c.state = s;
                c.terminal = term;
                c.type = "reduce/reduce";
                c.productions.push_back(cell.target);
                c.productions.push_back(pid);
                data.conflicts.push_back(std::move(c));
            }
            // 同格同 p 幂等跳过（T2.5 已将 (pid,dot) lookahead 合并为集合）
        } else {   // Accept（防御：接受态不应与归约并存）
            Conflict c;
            c.state = s;
            c.terminal = term;
            c.type = "reduce/reduce";
            c.productions.push_back(pid);
            data.conflicts.push_back(std::move(c));
        }
    };

    for (size_t i = 0; i < N; ++i) {
        for (const Lalr1Item& it : lalr.states[i].items) {
            const std::vector<int>& r = rhsOf(it.productionId);
            if (static_cast<size_t>(it.dot) != r.size()) {
                continue;   // 非完成项（T2.5 保证 0 <= dot <= rhs.size()）
            }
            if (it.productionId == data.augmentedProductionId) {
                // [S' -> S·]：lookahead 含 $ → Accept（接受态唯一）
                for (int la : it.lookaheads) {
                    if (la != 0) {
                        continue;
                    }
                    Action& cell = data.action[i][0];
                    if (cell.type == ActionType::Error) {
                        cell = Action{ ActionType::Accept, -1 };
                    } else {   // 防御：接受格已被占用
                        Conflict c;
                        c.state = static_cast<int>(i);
                        c.terminal = 0;
                        c.type = "reduce/reduce";
                        c.productions.push_back(it.productionId);
                        data.conflicts.push_back(std::move(c));
                    }
                }
            } else {
                for (int la : it.lookaheads) {
                    writeReduce(static_cast<int>(i), la, it.productionId);
                }
            }
        }
    }

    return Result<ParseTableData>::success(std::move(data));
}

} // namespace compiler::parser
