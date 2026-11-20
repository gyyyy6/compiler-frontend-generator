#pragma once

// 任务二 LR 视图填充工具（T2.10）
// 三个子 Tab（LR(0) 项目集 / LALR(1) 项目集 / 分析表）共用：
//   - 项目集表格：行=状态，列=状态/起始/接受/项目集/转移；项目 A -> α·β
//     （LALR 附 lookahead 集合），多项目换行、超阈值截断 + tooltip
//     （沿用 T1.9 AutomatonTableView 的样式惯例）；
//   - 分析表：行=状态，两行分组表头 ACTION（终结符 + $）/ GOTO（非终结符）；
//     冲突单元格高亮（背景色+加粗+tooltip，T2.10 用户要求）。
// 纯 Qt Widgets（gui 层）；数据源为任务二算法产物（零 Qt）。

class QTableWidget;
class QString;

namespace compiler {
struct Grammar;
}

namespace compiler::parser {
struct Lalr1Data;
struct Lr0AutomatonData;
struct ParseTableData;
}

namespace compiler::gui {

// 填充 LR(0) 项目集表（表格会被清空并重建）
void populateLr0Table(QTableWidget* table, const Grammar& g, const parser::Lr0AutomatonData& lr0);

// 填充 LALR(1) 项目集表（项目含 lookahead 集合）
void populateLalr1Table(QTableWidget* table, const Grammar& g, const parser::Lalr1Data& lalr);

// 填充 LALR(1) ACTION/GOTO 分析表（冲突单元格高亮）
void populateParseTable(QTableWidget* table, const Grammar& g, const parser::ParseTableData& pt);

// 渲染冲突报告文本（分析表 Tab 底部清单；无冲突时返回"无冲突"）
QString renderConflictReport(const Grammar& g, const parser::ParseTableData& pt);

} // namespace compiler::gui
