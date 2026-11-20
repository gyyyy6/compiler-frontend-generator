#pragma once

// NFA/DFA 状态转换表填充工具（T1.9）
// 三个子 Tab（NFA/DFA/最小DFA）共用：行=状态，列=状态/起始/接受/编码/转移。
// 大字母表按"连续字符同目标合并为区间"压缩显示；超阈值截断 + 整行 tooltip。
// NFA 边列表：单元格只显示"N 条边"，完整列表放 tooltip（T1.9 优化）。
// 高亮策略：不填背景色（兼容深色/浅色主题），只用文字区分——起始列 "→"、
// 接受列 "是"、编码列 tokenCode，标记列加粗（T1.9 深色主题修正）。

class QTableWidget;

namespace compiler {
struct Dfa;
struct Nfa;
}

namespace compiler::gui {

// 填充 NFA 转换表（表格会被清空并重建）
void populateNfaTable(QTableWidget* table, const Nfa& nfa);

// 填充 DFA / 最小 DFA 转换表
void populateDfaTable(QTableWidget* table, const Dfa& dfa);

} // namespace compiler::gui
