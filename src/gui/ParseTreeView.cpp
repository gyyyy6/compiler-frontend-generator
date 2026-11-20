#include "ParseTreeView.h"

#include <algorithm>
#include <string>

#include <QLabel>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include "ParseTree.h"

namespace compiler::gui {
namespace {

QString tokenTooltip(const Token& token)
{
    return QStringLiteral("code=%1\nname=%2\nline=%3, col=%4")
        .arg(token.code)
        .arg(QString::fromStdString(token.name))
        .arg(token.loc.line)
        .arg(token.loc.column);
}

QTreeWidgetItem* buildNode(const ParseTreeNode& node, const ParseTree& tree)
{
    auto* item = new QTreeWidgetItem;
    if (node.isLeaf()) {
        const QString text = node.text.empty()
            ? QStringLiteral("(ε)")
            : QString::fromStdString(node.text);
        item->setText(0, text);
        if (node.tokenIndex >= 0 &&
            node.tokenIndex < static_cast<int>(tree.tokens.size())) {
            item->setToolTip(0, tokenTooltip(
                tree.tokens[static_cast<size_t>(node.tokenIndex)]));
        }
    } else {
        QString text = QString::fromStdString(node.text);
        if (node.productionId >= 0) {
            text += QStringLiteral(" [prod %1]").arg(node.productionId);
        }
        item->setText(0, text);
    }
    for (const auto& child : node.children) {
        item->addChild(buildNode(*child, tree));
    }
    return item;
}

void collectStats(const ParseTreeNode& node, int depth, int& nodes, int& leaves, int& maxDepth)
{
    ++nodes;
    maxDepth = std::max(maxDepth, depth);
    if (node.isLeaf()) {
        ++leaves;
    }
    for (const auto& child : node.children) {
        collectStats(*child, depth + 1, nodes, leaves, maxDepth);
    }
}

} // namespace

void populateParseTree(QTreeWidget* tree, const ParseTree& parseTree, QLabel* statsLabel)
{
    tree->clear();
    if (!parseTree.root) {
        if (statsLabel) {
            statsLabel->setText(QStringLiteral("语法树：空"));
        }
        return;
    }

    tree->setColumnCount(1);
    tree->setHeaderLabel(QStringLiteral("语法树"));
    tree->addTopLevelItem(buildNode(*parseTree.root, parseTree));
    tree->expandAll();

    if (statsLabel) {
        int nodes = 0, leaves = 0, maxDepth = 0;
        collectStats(*parseTree.root, 1, nodes, leaves, maxDepth);
        statsLabel->setText(QStringLiteral("节点 %1 · 叶子 %2 · token %3 · 深度 %4")
                                .arg(nodes)
                                .arg(leaves)
                                .arg(parseTree.tokens.size())
                                .arg(maxDepth));
    }
}

} // namespace compiler::gui
