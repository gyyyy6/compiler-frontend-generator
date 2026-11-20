#pragma once

// 语法树 QTreeWidget 填充工具（T2.10）
// 方案一：QTreeWidget 逐层展开——内部节点显示 "符号名 [prod N]"（与化简后文法
// Tab 的产生式 id 对照），叶子显示词素（tooltip 含 token code/name/行列）。
// 纯 Qt Widgets（gui 层）；数据源 core ParseTree。

class QLabel;
class QTreeWidget;

namespace compiler {
struct ParseTree;
}

namespace compiler::gui {

// 填充语法树；statsLabel 非空时回填统计文本（节点数/叶子数/token 数/深度）
void populateParseTree(QTreeWidget* tree, const ParseTree& parseTree,
                       QLabel* statsLabel = nullptr);

} // namespace compiler::gui
