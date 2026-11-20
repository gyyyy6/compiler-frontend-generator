#include "AutomatonTableView.h"

#include <map>
#include <vector>

#include <QAbstractItemView>
#include <QFont>
#include <QHeaderView>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>

#include "DFA.h"
#include "NFA.h"

namespace compiler::gui {
namespace {

constexpr int kMaxShownRanges = 8;   // 转移条目显示阈值（超过截断 + tooltip）

// 单字符可打印化（控制字符用转义/十六进制）
QString charLabel(char c)
{
    switch (c) {
    case ' ':  return QStringLiteral("␣");
    case '\t': return QStringLiteral("\\t");
    case '\r': return QStringLiteral("\\r");
    case '\n': return QStringLiteral("\\n");
    default: {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F) {
            return QStringLiteral("0x%1").arg(u, 2, 16, QLatin1Char('0'));
        }
        return QString::fromLatin1(&c, 1);
    }
    }
}

struct CharRange { char from; char to; int target; };

// 连续字符且目标相同的转移合并为区间（std::map 有序，天然相邻）
std::vector<CharRange> mergeTransitions(const std::map<char, int>& trans)
{
    std::vector<CharRange> ranges;
    for (const auto& entry : trans) {
        const char ch = entry.first;
        const int target = entry.second;
        if (!ranges.empty() &&
            static_cast<int>(ranges.back().to) + 1 == static_cast<int>(ch) &&
            ranges.back().target == target) {
            ranges.back().to = ch;
        } else {
            ranges.push_back({ch, ch, target});
        }
    }
    return ranges;
}

QString rangeLabel(const CharRange& r)
{
    const QString chs = (r.from == r.to)
        ? charLabel(r.from)
        : QStringLiteral("%1-%2").arg(charLabel(r.from), charLabel(r.to));
    return QStringLiteral("%1→%2").arg(chs).arg(r.target);
}

// 创建单元格：不填背景色（深色/浅色主题均兼容），可带 tooltip
QTableWidgetItem* makeItem(const QString& text, const QString& tooltip = {})
{
    auto* item = new QTableWidgetItem(text);
    if (!tooltip.isEmpty()) {
        item->setToolTip(tooltip);
    }
    return item;
}

// 标记列（起始/接受/编码）加粗以示强调：纯文字区分，不依赖主题配色
void makeBold(QTableWidgetItem* item)
{
    if (!item) {
        return;
    }
    QFont f = item->font();
    f.setBold(true);
    item->setFont(f);
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

QString joinNfaStates(const std::vector<int>& states)
{
    QStringList parts;
    for (int s : states) {
        parts << QString::number(s);
    }
    return QStringLiteral("{%1}").arg(parts.join(QStringLiteral(", ")));
}

} // namespace

void populateNfaTable(QTableWidget* table, const Nfa& nfa)
{
    initTable(table, 6, {QStringLiteral("状态"), QStringLiteral("起始"), QStringLiteral("接受"),
                         QStringLiteral("编码"), QStringLiteral("优先级"), QStringLiteral("边")});
    table->setRowCount(static_cast<int>(nfa.states.size()));
    for (int i = 0; i < static_cast<int>(nfa.states.size()); ++i) {
        const NfaState& st = nfa.states[i];
        const bool isStart = (st.id == nfa.startState);
        const bool isAccept = st.isAccept;

        table->setItem(i, 0, makeItem(QString::number(st.id)));

        auto* startItem = makeItem(isStart ? QStringLiteral("→") : QString());
        if (isStart) {
            makeBold(startItem);
        }
        table->setItem(i, 1, startItem);

        auto* acceptItem = makeItem(isAccept ? QStringLiteral("是") : QStringLiteral("否"));
        if (isAccept) {
            makeBold(acceptItem);
        }
        table->setItem(i, 2, acceptItem);

        auto* codeItem = makeItem(isAccept ? QString::number(st.tokenCode) : QStringLiteral("—"));
        if (isAccept) {
            makeBold(codeItem);
        }
        table->setItem(i, 3, codeItem);

        auto* priItem = makeItem(isAccept ? QString::number(st.priority) : QStringLiteral("—"));
        if (isAccept) {
            makeBold(priItem);
        }
        table->setItem(i, 4, priItem);

        QStringList edgeStrs;
        for (const NfaEdge& e : st.edges) {
            switch (e.type) {
            case NfaEdge::Type::Epsilon:
                edgeStrs << QStringLiteral("ε→%1").arg(e.target);
                break;
            case NfaEdge::Type::Char:
                edgeStrs << QStringLiteral("%1→%2").arg(charLabel(e.ch)).arg(e.target);
                break;
            case NfaEdge::Type::CharClass:
                edgeStrs << QStringLiteral("[%1]→%2")
                                .arg(QString::fromStdString(e.charClass))
                                .arg(e.target);
                break;
            case NfaEdge::Type::NegatedClass:
                edgeStrs << QStringLiteral("~[%1]→%2")
                                .arg(QString::fromStdString(e.charClass))
                                .arg(e.target);
                break;
            }
        }
        // 单元格只显示"N 条边"，完整列表放 tooltip（T1.9 优化）
        const QString cell = edgeStrs.isEmpty() ? QStringLiteral("—")
                                                : QStringLiteral("%1 条边").arg(edgeStrs.size());
        table->setItem(i, 5, makeItem(cell, edgeStrs.join(QStringLiteral("; "))));
    }
}

void populateDfaTable(QTableWidget* table, const Dfa& dfa)
{
    initTable(table, 5, {QStringLiteral("状态"), QStringLiteral("起始"), QStringLiteral("接受"),
                         QStringLiteral("编码"), QStringLiteral("转移")});
    table->setRowCount(static_cast<int>(dfa.states.size()));
    for (int i = 0; i < static_cast<int>(dfa.states.size()); ++i) {
        const DfaState& st = dfa.states[i];
        const bool isStart = (st.id == dfa.startState);
        const bool isAccept = st.isAccept;
        const QString tooltip = st.nfaStates.empty()
            ? QString()
            : QStringLiteral("对应 NFA 状态子集：%1").arg(joinNfaStates(st.nfaStates));

        table->setItem(i, 0, makeItem(QString::number(st.id), tooltip));

        auto* startItem = makeItem(isStart ? QStringLiteral("→") : QString());
        if (isStart) {
            makeBold(startItem);
        }
        table->setItem(i, 1, startItem);

        auto* acceptItem = makeItem(isAccept ? QStringLiteral("是") : QStringLiteral("否"));
        if (isAccept) {
            makeBold(acceptItem);
        }
        table->setItem(i, 2, acceptItem);

        auto* codeItem = makeItem(isAccept ? QString::number(st.tokenCode) : QStringLiteral("—"));
        if (isAccept) {
            makeBold(codeItem);
        }
        table->setItem(i, 3, codeItem);

        // 转移：区间合并 + 超阈值截断，完整列表放 tooltip
        const auto ranges = mergeTransitions(st.transitions);
        QString full;
        for (const CharRange& r : ranges) {
            if (!full.isEmpty()) {
                full += QStringLiteral(", ");
            }
            full += rangeLabel(r);
        }
        if (full.isEmpty()) {
            full = QStringLiteral("—");
        }
        QString shown = full;
        if (static_cast<int>(ranges.size()) > kMaxShownRanges) {
            QString first;
            for (int k = 0; k < kMaxShownRanges; ++k) {
                if (k) {
                    first += QStringLiteral(", ");
                }
                first += rangeLabel(ranges[k]);
            }
            shown = first + QStringLiteral(" …（共 %1 条）").arg(ranges.size());
        }
        table->setItem(i, 4, makeItem(shown, full));
    }
}

} // namespace compiler::gui
