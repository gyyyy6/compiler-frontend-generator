#include "LrTableView.h"

#include <map>
#include <set>
#include <string>
#include <vector>

#include <QAbstractItemView>
#include <QColor>
#include <QFont>
#include <QHeaderView>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>

#include "Grammar.h"
#include "Lalr1Builder.h"
#include "Lr0Automaton.h"
#include "ParseTable.h"

namespace compiler::gui {
namespace {

constexpr int kMaxShownItems = 8;        // 项目集单元格显示阈值
constexpr int kMaxShownTransitions = 8;  // 转移单元格显示阈值

QTableWidgetItem* makeItem(const QString& text, const QString& tooltip = {})
{
    auto* item = new QTableWidgetItem(text);
    if (!tooltip.isEmpty()) {
        item->setToolTip(tooltip);
    }
    return item;
}

void makeBold(QTableWidgetItem* item)
{
    if (!item) {
        return;
    }
    QFont f = item->font();
    f.setBold(true);
    item->setFont(f);
}

// 冲突单元格：琥珀底 + 黑字 + 加粗（深浅主题均清晰；T2.10 用户要求高亮）
void markConflict(QTableWidgetItem* item, const QString& detail)
{
    item->setBackground(QColor(0xF0, 0xB4, 0x29));
    item->setForeground(QColor(0x00, 0x00, 0x00));
    makeBold(item);
    item->setToolTip(detail);
}

void initTable(QTableWidget* table, int columns, const QStringList& headers)
{
    table->clear();
    table->setColumnCount(columns);
    table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
}

QString symbolName(const Grammar& g, int symbolId)
{
    const Symbol* s = g.symbol(symbolId);
    return s ? QString::fromStdString(s->name)
             : QStringLiteral("?%1").arg(symbolId);
}

// 产生式右部路由：增广产生式 S' -> S 不写入 g.productions（T2.4 约定）
const std::vector<int>& rhsOf(const Grammar& g, int pid,
                              const std::vector<int>& augRhs, int augPid)
{
    return pid == augPid ? augRhs : g.productions[static_cast<size_t>(pid)].rhs;
}

// [A -> α·β]：dot 位于 α|β 之间（dot ∈ [0, rhs.size()]）；ε 产生式显示 [A -> ·]
QString renderItem(const Grammar& g, int pid, int dot,
                   const std::vector<int>& augRhs, int augPid,
                   const std::string& augLhs, const QString& lookaheadSuffix = {})
{
    const std::vector<int>& rhs = rhsOf(g, pid, augRhs, augPid);
    const QString lhs = (pid == augPid)
        ? QString::fromStdString(augLhs)
        : symbolName(g, g.productions[static_cast<size_t>(pid)].lhs);

    QString body;
    for (int i = 0; i <= static_cast<int>(rhs.size()); ++i) {
        if (i > 0) {
            body += QStringLiteral(" ");
        }
        if (i == dot) {
            body += QStringLiteral("·");
        }
        if (i < static_cast<int>(rhs.size())) {
            body += symbolName(g, rhs[static_cast<size_t>(i)]);
        }
    }
    return lhs + QStringLiteral(" -> ") + body + lookaheadSuffix;
}

QString renderLookaheads(const Grammar& g, const std::set<int>& lookaheads)
{
    QStringList parts;
    for (int la : lookaheads) {
        parts << symbolName(g, la);
    }
    return QStringLiteral("  {%1}").arg(parts.join(QStringLiteral(", ")));
}

// 多条目单元格：换行显示，超阈值截断 + "…（共 N 项）"；tooltip 放完整列表
QString joinOrTruncate(const QStringList& parts, int maxShown)
{
    if (parts.isEmpty()) {
        return QStringLiteral("—");
    }
    if (parts.size() <= maxShown) {
        return parts.join(QStringLiteral("\n"));
    }
    QStringList first;
    for (int k = 0; k < maxShown; ++k) {
        first << parts[k];
    }
    return first.join(QStringLiteral("\n")) +
           QStringLiteral("\n…（共 %1 项）").arg(parts.size());
}

QString conflictDetail(const Grammar& g, const parser::Conflict& c,
                       const parser::ParseTableData& pt)
{
    QStringList prods;
    for (int pid : c.productions) {
        prods << QString::number(pid);
    }
    // 表内保留动作（决策点 6：S/R 保留 Shift；R/R 保留低产生式 id）
    QString kept = QStringLiteral("—");
    const auto it = pt.action[static_cast<size_t>(c.state)].find(c.terminal);
    if (it != pt.action[static_cast<size_t>(c.state)].end()) {
        switch (it->second.type) {
        case parser::ActionType::Shift:
            kept = QStringLiteral("Shift(%1)").arg(it->second.target);
            break;
        case parser::ActionType::Reduce:
            kept = QStringLiteral("Reduce(%1)").arg(it->second.target);
            break;
        default:
            break;
        }
    }
    return QStringLiteral("%1：状态 %2 / 终结符 %3，涉及产生式 %4；表内保留 %5")
        .arg(QString::fromStdString(c.type))
        .arg(c.state)
        .arg(symbolName(g, c.terminal))
        .arg(prods.join(QStringLiteral(", ")))
        .arg(kept);
}

} // namespace

void populateLr0Table(QTableWidget* table, const Grammar& g,
                      const parser::Lr0AutomatonData& lr0)
{
    initTable(table, 5, {QStringLiteral("状态"), QStringLiteral("起始"),
                         QStringLiteral("接受"), QStringLiteral("项目集"),
                         QStringLiteral("转移")});
    table->setRowCount(static_cast<int>(lr0.states.size()));

    const int augRhsSize = static_cast<int>(lr0.augmentedProductionRhs.size());
    for (size_t i = 0; i < lr0.states.size(); ++i) {
        const parser::Lr0State& st = lr0.states[i];
        const int row = static_cast<int>(i);
        const bool isStart = (st.id == 0);
        bool isAccept = false;
        for (const parser::Item& item : st.items) {
            if (item.productionId == lr0.augmentedProductionId && item.dot == augRhsSize) {
                isAccept = true;
                break;
            }
        }

        table->setItem(row, 0, makeItem(QString::number(st.id)));

        auto* startItem = makeItem(isStart ? QStringLiteral("→") : QString());
        if (isStart) {
            makeBold(startItem);
        }
        table->setItem(row, 1, startItem);

        auto* acceptItem = makeItem(isAccept ? QStringLiteral("是") : QStringLiteral("否"));
        if (isAccept) {
            makeBold(acceptItem);
        }
        table->setItem(row, 2, acceptItem);

        QStringList items;
        for (const parser::Item& item : st.items) {
            items << renderItem(g, item.productionId, item.dot,
                                lr0.augmentedProductionRhs, lr0.augmentedProductionId,
                                lr0.augmentedStartSymbolName);
        }
        table->setItem(row, 3, makeItem(joinOrTruncate(items, kMaxShownItems),
                                        items.join(QStringLiteral("\n"))));

        QStringList trans;
        for (const auto& entry : lr0.transitions) {
            if (entry.first.first != st.id) {
                continue;
            }
            trans << QStringLiteral("%1→%2")
                         .arg(symbolName(g, entry.first.second))
                         .arg(entry.second);
        }
        table->setItem(row, 4, makeItem(joinOrTruncate(trans, kMaxShownTransitions),
                                        trans.join(QStringLiteral(", "))));
    }
}

void populateLalr1Table(QTableWidget* table, const Grammar& g,
                        const parser::Lalr1Data& lalr)
{
    initTable(table, 5, {QStringLiteral("状态"), QStringLiteral("起始"),
                         QStringLiteral("接受"), QStringLiteral("项目集"),
                         QStringLiteral("转移")});
    table->setRowCount(static_cast<int>(lalr.states.size()));

    const int augRhsSize = static_cast<int>(lalr.augmentedProductionRhs.size());
    for (size_t i = 0; i < lalr.states.size(); ++i) {
        const parser::Lalr1State& st = lalr.states[i];
        const int row = static_cast<int>(i);
        const bool isStart = (st.id == 0);
        bool isAccept = false;
        for (const parser::Lalr1Item& item : st.items) {
            if (item.productionId == lalr.augmentedProductionId && item.dot == augRhsSize) {
                isAccept = true;
                break;
            }
        }

        table->setItem(row, 0, makeItem(QString::number(st.id)));

        auto* startItem = makeItem(isStart ? QStringLiteral("→") : QString());
        if (isStart) {
            makeBold(startItem);
        }
        table->setItem(row, 1, startItem);

        auto* acceptItem = makeItem(isAccept ? QStringLiteral("是") : QStringLiteral("否"));
        if (isAccept) {
            makeBold(acceptItem);
        }
        table->setItem(row, 2, acceptItem);

        QStringList items;
        for (const parser::Lalr1Item& item : st.items) {
            items << renderItem(g, item.productionId, item.dot,
                                lalr.augmentedProductionRhs, lalr.augmentedProductionId,
                                lalr.augmentedStartSymbolName,
                                renderLookaheads(g, item.lookaheads));
        }
        table->setItem(row, 3, makeItem(joinOrTruncate(items, kMaxShownItems),
                                        items.join(QStringLiteral("\n"))));

        QStringList trans;
        for (const auto& entry : lalr.transitions) {
            if (entry.first.first != st.id) {
                continue;
            }
            trans << QStringLiteral("%1→%2")
                         .arg(symbolName(g, entry.first.second))
                         .arg(entry.second);
        }
        table->setItem(row, 4, makeItem(joinOrTruncate(trans, kMaxShownTransitions),
                                        trans.join(QStringLiteral(", "))));
    }
}

void populateParseTable(QTableWidget* table, const Grammar& g,
                        const parser::ParseTableData& pt)
{
    const std::vector<int> terminals = g.terminalIds();
    const std::vector<int> nonterminals = g.nonterminalIds();
    const int nAction = static_cast<int>(terminals.size()) + 1;  // +$（id 0，接受）
    const int nGoto = static_cast<int>(nonterminals.size());
    const int nCols = 1 + nAction + nGoto;

    table->clear();
    // 第 0 行为分组行（ACTION/GOTO span），第 1..N 行为状态数据
    table->setRowCount(static_cast<int>(pt.action.size()) + 1);
    table->setColumnCount(nCols);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);

    // 表头（单行）：状态 + 符号名
    table->setHorizontalHeaderItem(0, new QTableWidgetItem(QStringLiteral("状态")));
    for (int i = 0; i < nAction; ++i) {
        const int symId = (i < static_cast<int>(terminals.size()))
                              ? terminals[static_cast<size_t>(i)]
                              : 0;  // 最后一列为 $（id 0）
        table->setHorizontalHeaderItem(1 + i, new QTableWidgetItem(symbolName(g, symId)));
    }
    for (int i = 0; i < nGoto; ++i) {
        table->setHorizontalHeaderItem(1 + nAction + i,
                                       new QTableWidgetItem(symbolName(g, nonterminals[static_cast<size_t>(i)])));
    }

    // 分组行（第 0 行）：ACTION / GOTO
    table->setItem(0, 0, makeItem(QStringLiteral("组")));
    table->setItem(0, 1, makeItem(QStringLiteral("ACTION")));
    table->setSpan(0, 1, 1, nAction);
    if (nGoto > 0) {
        table->setItem(0, 1 + nAction, makeItem(QStringLiteral("GOTO")));
        table->setSpan(0, 1 + nAction, 1, nGoto);
    }

    for (size_t st = 0; st < pt.action.size(); ++st) {
        const int row = static_cast<int>(st) + 1;
        table->setItem(row, 0, makeItem(QString::number(st)));

        for (int i = 0; i < nAction; ++i) {
            const int symId = (i < static_cast<int>(terminals.size()))
                                  ? terminals[static_cast<size_t>(i)]
                                  : 0;
            const auto it = pt.action[st].find(symId);
            if (it == pt.action[st].end()) {
                continue;  // Error 缺省：留空
            }
            const parser::Action& act = it->second;
            QString text;
            switch (act.type) {
            case parser::ActionType::Shift:  text = QStringLiteral("s%1").arg(act.target); break;
            case parser::ActionType::Reduce: text = QStringLiteral("r%1").arg(act.target); break;
            case parser::ActionType::Accept: text = QStringLiteral("acc"); break;
            case parser::ActionType::Error:  text = QString(); break;
            }
            auto* item = makeItem(text);
            for (const parser::Conflict& c : pt.conflicts) {
                if (c.state == static_cast<int>(st) && c.terminal == symId) {
                    markConflict(item, conflictDetail(g, c, pt));
                    break;
                }
            }
            table->setItem(row, 1 + i, item);
        }

        for (int i = 0; i < nGoto; ++i) {
            const int symId = nonterminals[static_cast<size_t>(i)];
            const auto it = pt.gotoTable[st].find(symId);
            if (it == pt.gotoTable[st].end()) {
                continue;
            }
            table->setItem(row, 1 + nAction + i, makeItem(QString::number(it->second)));
        }
    }
}

QString renderConflictReport(const Grammar& g, const parser::ParseTableData& pt)
{
    if (pt.conflicts.empty()) {
        return QStringLiteral("无冲突");
    }
    QStringList lines;
    for (const parser::Conflict& c : pt.conflicts) {
        lines << QStringLiteral("- ") + conflictDetail(g, c, pt);
    }
    return QStringLiteral("冲突清单（%1 处）：\n%2")
        .arg(pt.conflicts.size())
        .arg(lines.join(QStringLiteral("\n")));
}

} // namespace compiler::gui
