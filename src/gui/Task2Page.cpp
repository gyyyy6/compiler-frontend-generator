#include "Task2Page.h"

#include <set>
#include <string>
#include <vector>

#include <QAbstractItemView>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QThread>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "Diagnostic.h"
#include "FirstFollow.h"
#include "Grammar.h"
#include "GrammarSimplifier.h"
#include "LrTableView.h"
#include "ParseTreeView.h"
#include "ParserPipeline.h"
#include "Task2PipelineWorker.h"

namespace compiler::gui {
namespace {

QString symbolName(const Grammar& g, int symbolId)
{
    const Symbol* s = g.symbol(symbolId);
    return s ? QString::fromStdString(s->name)
             : QStringLiteral("?%1").arg(symbolId);
}

// 集合 → "a, b, c"（空集显示 ∅）
QString joinSet(const Grammar& g, const std::vector<std::set<int>>& sets, int symbolId)
{
    QStringList parts;
    if (symbolId >= 0 && static_cast<size_t>(symbolId) < sets.size()) {
        for (int s : sets[static_cast<size_t>(symbolId)]) {
            parts << symbolName(g, s);
        }
    }
    return parts.isEmpty() ? QStringLiteral("∅") : parts.join(QStringLiteral(", "));
}

// Tab3 First/Follow：每非终结符一行
void populateFirstFollowTable(QTableWidget* table, const Grammar& g,
                              const grammar::FirstFollowSets& sets)
{
    const std::vector<int> nts = g.nonterminalIds();
    table->clear();
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({QStringLiteral("非终结符"), QStringLiteral("First"),
                                      QStringLiteral("Follow"), QStringLiteral("Nullable")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setRowCount(static_cast<int>(nts.size()));
    for (size_t i = 0; i < nts.size(); ++i) {
        const int sid = nts[i];
        const bool nullable = (static_cast<size_t>(sid) < sets.nullable.size())
                                  ? sets.nullable[static_cast<size_t>(sid)]
                                  : false;
        table->setItem(static_cast<int>(i), 0, new QTableWidgetItem(symbolName(g, sid)));
        table->setItem(static_cast<int>(i), 1, new QTableWidgetItem(joinSet(g, sets.first, sid)));
        table->setItem(static_cast<int>(i), 2, new QTableWidgetItem(joinSet(g, sets.follow, sid)));
        table->setItem(static_cast<int>(i), 3,
                       new QTableWidgetItem(nullable ? QStringLiteral("是")
                                                     : QStringLiteral("否")));
    }
}

// Tab2 化简后文法：删除日志 + 带 id 产生式（id 与语法树 productionId 对照）
QString renderSimplifiedText(const pipeline::ParserPipelineSnapshot& snap)
{
    QString out;
    out += QStringLiteral("【化简摘要】%1\n\n").arg(QString::fromStdString(snap.simplifyLog.summary));
    if (!snap.simplifyLog.removedSymbols.empty()) {
        out += QStringLiteral("【删除的非终结符 %1 个】\n").arg(snap.simplifyLog.removedSymbols.size());
        for (const auto& s : snap.simplifyLog.removedSymbols) {
            out += QStringLiteral("  %1（%2）\n").arg(QString::fromStdString(s.name),
                                                      QString::fromStdString(s.reason));
        }
        out += QStringLiteral("\n");
    }
    if (!snap.simplifyLog.removedProductions.empty()) {
        out += QStringLiteral("【删除的产生式 %1 条】\n").arg(snap.simplifyLog.removedProductions.size());
        for (const auto& p : snap.simplifyLog.removedProductions) {
            out += QStringLiteral("  #%1（%2）\n").arg(p.originalId)
                                                   .arg(QString::fromStdString(p.reason));
        }
        out += QStringLiteral("\n");
    }
    out += QStringLiteral("【化简后文法（%1 条产生式）】\n")
               .arg(snap.simplifiedGrammar.productions.size());
    for (const Production& p : snap.simplifiedGrammar.productions) {
        QString rhs;
        for (int s : p.rhs) {
            if (!rhs.isEmpty()) {
                rhs += QStringLiteral(" ");
            }
            rhs += symbolName(snap.simplifiedGrammar, s);
        }
        if (rhs.isEmpty()) {
            rhs = QStringLiteral("#");   // ε 产生式（rhs={}）
        }
        out += QStringLiteral("%1: %2 -> %3 ;\n")
                   .arg(p.id)
                   .arg(symbolName(snap.simplifiedGrammar, p.lhs))
                   .arg(rhs);
    }
    return out;
}

} // namespace

Task2Page::Task2Page(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);

    createToolbar();
    createStepTabs();

    layout->addWidget(toolbar_, 0);
    layout->addWidget(steps_, 1);

    // ---- 流水线工作线程 ----
    worker_ = new Task2PipelineWorker;
    workerThread_ = new QThread;   // 无父对象，析构显式按序删除
    worker_->moveToThread(workerThread_);

    connect(worker_, &Task2PipelineWorker::stageStarted,
            this, &Task2Page::onStageStarted);
    connect(worker_, &Task2PipelineWorker::logMessage,
            this, &Task2Page::logMessage);   // 信号转发（排队连接）
    connect(worker_, &Task2PipelineWorker::finished,
            this, &Task2Page::onFinished);

    workerThread_->start();
}

Task2Page::~Task2Page()
{
    // 运行中关闭窗口不崩溃：先请求停止（阶段边界生效），再退出并等待线程，
    // 最后按序删除 worker 与线程（与 QObject 子对象析构顺序无关，均为无父对象）。
    if (workerThread_) {
        worker_->requestStop();
        workerThread_->quit();
        workerThread_->wait();
    }
    delete worker_;
    delete workerThread_;
}

void Task2Page::createToolbar()
{
    toolbar_ = new QWidget(this);
    auto* root = new QVBoxLayout(toolbar_);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);

    auto* row1 = new QHBoxLayout;
    openBnfBtn_ = new QPushButton(tr("打开 .bnf 文件"), toolbar_);
    openSampleLexBtn_ = new QPushButton(tr("打开 sample.lex 文件"), toolbar_);
    runAllBtn_ = new QPushButton(tr("一键运行"), toolbar_);
    runAllBtn_->setDefault(true);

    runBnfParseBtn_ = new QPushButton(tr("只解析"), toolbar_);
    runSimplifyBtn_ = new QPushButton(tr("只化简"), toolbar_);
    runFirstFollowBtn_ = new QPushButton(tr("只 First-Follow"), toolbar_);

    connect(openBnfBtn_, &QPushButton::clicked, this, &Task2Page::openBnfFile);
    connect(openSampleLexBtn_, &QPushButton::clicked, this, &Task2Page::openSampleLexFile);
    connect(runAllBtn_, &QPushButton::clicked, this, &Task2Page::runAll);
    connect(runBnfParseBtn_, &QPushButton::clicked, this, &Task2Page::runBnfParseOnly);
    connect(runSimplifyBtn_, &QPushButton::clicked, this, &Task2Page::runSimplifyOnly);
    connect(runFirstFollowBtn_, &QPushButton::clicked, this, &Task2Page::runFirstFollowOnly);

    row1->addWidget(openBnfBtn_);
    row1->addWidget(openSampleLexBtn_);
    row1->addSpacing(12);
    row1->addWidget(runAllBtn_);
    row1->addSpacing(12);
    row1->addWidget(runBnfParseBtn_);
    row1->addWidget(runSimplifyBtn_);
    row1->addWidget(runFirstFollowBtn_);
    row1->addStretch();
    root->addLayout(row1);

    auto* row2 = new QHBoxLayout;
    runLr0Btn_ = new QPushButton(tr("只 LR(0)"), toolbar_);
    runLalr1Btn_ = new QPushButton(tr("只 LALR"), toolbar_);
    runParseTableBtn_ = new QPushButton(tr("只分析表"), toolbar_);
    runDriveBtn_ = new QPushButton(tr("只解析语法树"), toolbar_);
    runIrBtn_ = new QPushButton(tr("只生成 IR"), toolbar_);

    connect(runLr0Btn_, &QPushButton::clicked, this, &Task2Page::runLr0Only);
    connect(runLalr1Btn_, &QPushButton::clicked, this, &Task2Page::runLalr1Only);
    connect(runParseTableBtn_, &QPushButton::clicked, this, &Task2Page::runParseTableOnly);
    connect(runDriveBtn_, &QPushButton::clicked, this, &Task2Page::runDriveOnly);
    connect(runIrBtn_, &QPushButton::clicked, this, &Task2Page::runIrOnly);

    row2->addWidget(runLr0Btn_);
    row2->addWidget(runLalr1Btn_);
    row2->addWidget(runParseTableBtn_);
    row2->addWidget(runDriveBtn_);
    row2->addWidget(runIrBtn_);
    row2->addStretch();
    root->addLayout(row2);

    auto* row3 = new QHBoxLayout;
    bnfPathLabel_ = new QLabel(tr("BNF：未打开"), toolbar_);
    sampleLexPathLabel_ = new QLabel(tr("sample.lex：未打开"), toolbar_);
    statusLabel_ = new QLabel(tr("就绪"), toolbar_);
    bnfPathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sampleLexPathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusLabel_->setStyleSheet(QStringLiteral("font-weight:bold;"));
    row3->addWidget(bnfPathLabel_);
    row3->addSpacing(24);
    row3->addWidget(sampleLexPathLabel_);
    row3->addStretch();
    row3->addWidget(statusLabel_);
    root->addLayout(row3);
}

void Task2Page::createStepTabs()
{
    steps_ = new QTabWidget(this);
    const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    // 1) BNF 文法（可编辑）
    auto* bnfTab = new QWidget(steps_);
    auto* bnfLayout = new QVBoxLayout(bnfTab);
    bnfEditor_ = new QPlainTextEdit(bnfTab);
    bnfEditor_->setFont(fixed);
    bnfEditor_->setLineWrapMode(QPlainTextEdit::NoWrap);
    bnfEditor_->setPlaceholderText(tr("此处显示/编辑 .bnf 文法文件内容"));
    bnfLayout->addWidget(bnfEditor_, 1);
    steps_->addTab(bnfTab, tr("BNF 文法"));

    // 2) 化简后文法（只读：删除日志 + 带 id 产生式）
    auto* simTab = new QWidget(steps_);
    auto* simLayout = new QVBoxLayout(simTab);
    simplifiedView_ = new QPlainTextEdit(simTab);
    simplifiedView_->setReadOnly(true);
    simplifiedView_->setFont(fixed);
    simplifiedView_->setLineWrapMode(QPlainTextEdit::NoWrap);
    simplifiedView_->setPlaceholderText(tr("GrammarSimplifier 删除日志 + 化简后产生式（带 id）"));
    simLayout->addWidget(simplifiedView_, 1);
    steps_->addTab(simTab, tr("化简后文法"));

    // 3) First/Follow
    firstFollowTable_ = new QTableWidget(steps_);
    steps_->addTab(firstFollowTable_, tr("First/Follow"));

    // 4) LR(0)+LALR(1) 项目集（内嵌子 Tab）
    itemSetTabs_ = new QTabWidget(steps_);
    lr0Table_ = new QTableWidget(itemSetTabs_);
    lalr1Table_ = new QTableWidget(itemSetTabs_);
    itemSetTabs_->addTab(lr0Table_, tr("LR(0) 项目集"));
    itemSetTabs_->addTab(lalr1Table_, tr("LALR(1) 项目集"));
    steps_->addTab(itemSetTabs_, tr("项目集 LR(0)+LALR(1)"));

    // 5) 分析表 + 冲突清单
    auto* tableTab = new QWidget(steps_);
    auto* tableLayout = new QVBoxLayout(tableTab);
    parseTable_ = new QTableWidget(tableTab);
    conflictView_ = new QPlainTextEdit(tableTab);
    conflictView_->setReadOnly(true);
    conflictView_->setFixedHeight(90);
    conflictView_->setPlaceholderText(tr("冲突清单（LALR(1) 冲突报告）"));
    tableLayout->addWidget(parseTable_, 1);
    tableLayout->addWidget(conflictView_, 0);
    steps_->addTab(tableTab, tr("分析表"));

    // 6) 语法树 + 四元式 IR
    auto* treeTab = new QWidget(steps_);
    auto* treeLayout = new QVBoxLayout(treeTab);
    treeStatsLabel_ = new QLabel(tr("语法树：空"), treeTab);
    parseTreeView_ = new QTreeWidget(treeTab);
    auto* irLabel = new QLabel(tr("四元式中间代码（IR）"), treeTab);
    irView_ = new QPlainTextEdit(treeTab);
    irView_->setReadOnly(true);
    irView_->setFont(fixed);
    irView_->setLineWrapMode(QPlainTextEdit::NoWrap);
    irView_->setPlaceholderText(tr("四元式 (op, arg1, arg2, result)"));
    treeLayout->addWidget(treeStatsLabel_, 0);
    treeLayout->addWidget(parseTreeView_, 3);
    treeLayout->addWidget(irLabel, 0);
    treeLayout->addWidget(irView_, 2);
    steps_->addTab(treeTab, tr("语法树"));
}

void Task2Page::openBnfFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("打开 .bnf 文法文件"), QString(),
        tr("BNF 文法 (*.bnf);;所有文件 (*)"));
    if (path.isEmpty()) {
        return;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit logMessage(tr("[Error] [input] 无法打开文法文件：%1").arg(path));
        return;
    }
    bnfPath_ = path;
    bnfPathLabel_->setText(tr("BNF：%1").arg(path));
    bnfEditor_->setPlainText(QString::fromUtf8(f.readAll()));
    clearArtifactTabs();   // 文法变更：清空下游产物（下次运行按快照重填）
    statusLabel_->setText(tr("已加载文法"));
    emit logMessage(tr("[Info] 已加载文法文件：%1").arg(path));
}

void Task2Page::openSampleLexFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("打开 sample.lex 单词编码文件"), QString(),
        tr("单词编码 (*.lex);;所有文件 (*)"));
    if (path.isEmpty()) {
        return;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit logMessage(tr("[Error] [input] 无法打开 sample.lex 文件：%1").arg(path));
        return;
    }
    sampleLexPath_ = path;
    sampleLexPathLabel_->setText(tr("sample.lex：%1").arg(path));
    clearArtifactTabs();
    statusLabel_->setText(tr("已加载 sample.lex"));
    emit logMessage(tr("[Info] 已加载 sample.lex 文件：%1").arg(path));
}

void Task2Page::runAll()
{
    startPipeline(pipeline::ParseStage::Ir);
}

void Task2Page::runBnfParseOnly()
{
    startPipeline(pipeline::ParseStage::BnfParse);
}

void Task2Page::runSimplifyOnly()
{
    startPipeline(pipeline::ParseStage::Simplify);
}

void Task2Page::runFirstFollowOnly()
{
    startPipeline(pipeline::ParseStage::FirstFollow);
}

void Task2Page::runLr0Only()
{
    startPipeline(pipeline::ParseStage::Lr0);
}

void Task2Page::runLalr1Only()
{
    startPipeline(pipeline::ParseStage::Lalr1);
}

void Task2Page::runParseTableOnly()
{
    startPipeline(pipeline::ParseStage::ParseTable);
}

void Task2Page::runDriveOnly()
{
    startPipeline(pipeline::ParseStage::Drive);
}

void Task2Page::runIrOnly()
{
    startPipeline(pipeline::ParseStage::Ir);
}

void Task2Page::startPipeline(pipeline::ParseStage target)
{
    if (busy_) {
        return;
    }
    if (bnfEditor_->toPlainText().trimmed().isEmpty()) {
        emit logMessage(tr("[Error] [input] 请先打开或输入 .bnf 文法文件"));
        return;
    }
    if (target >= pipeline::ParseStage::Drive && sampleLexPath_.isEmpty()) {
        emit logMessage(tr("[Error] [input] 语法树/IR 阶段需要 sample.lex（请先打开 sample.lex 文件）"));
        return;
    }

    // 每次运行重新读取 sample.lex（任务一可能重新生成），保证内容最新
    QString sampleLexText;
    if (!sampleLexPath_.isEmpty()) {
        QFile f(sampleLexPath_);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            emit logMessage(tr("[Error] [input] 无法读取 sample.lex 文件：%1").arg(sampleLexPath_));
            return;
        }
        sampleLexText = QString::fromUtf8(f.readAll());
    }

    setBusy(true);
    QMetaObject::invokeMethod(
        worker_, "runTask", Qt::QueuedConnection,
        Q_ARG(int, static_cast<int>(target)),
        Q_ARG(QString, bnfEditor_->toPlainText()),
        Q_ARG(QString, bnfPath_),
        Q_ARG(QString, sampleLexText),
        Q_ARG(QString, sampleLexPath_));
}

void Task2Page::setBusy(bool busy)
{
    busy_ = busy;
    const QList<QPushButton*> buttons = {
        openBnfBtn_, openSampleLexBtn_, runAllBtn_, runBnfParseBtn_, runSimplifyBtn_,
        runFirstFollowBtn_, runLr0Btn_, runLalr1Btn_, runParseTableBtn_,
        runDriveBtn_, runIrBtn_,
    };
    for (QPushButton* b : buttons) {
        if (b) {
            b->setEnabled(!busy);
        }
    }
    statusLabel_->setText(busy ? tr("执行中…") : tr("就绪"));
    if (busy) {
        emit statusMessage(tr("任务二：执行中…"));
    }
}

void Task2Page::onStageStarted(const QString& name)
{
    statusLabel_->setText(tr("正在：%1…").arg(name));
    emit logMessage(tr("[Info] [pipeline] 开始 %1").arg(name));
}

void Task2Page::onFinished(bool ok, const pipeline::ParserPipelineSnapshot& snap)
{
    setBusy(false);
    logDiagnostics(snap);
    applySnapshot(snap);

    if (ok) {
        statusLabel_->setText(tr("完成"));
        emit statusMessage(tr("任务二执行成功"));
        emit logMessage(tr("[Info] 任务二执行成功（已推进到阶段 %1）")
                            .arg(static_cast<int>(snap.validThrough)));
    } else {
        // 定位首个 Error 诊断的阶段，便于用户快速排查
        QString failStage = tr("未知");
        for (const Diagnostic& d : snap.diagnostics) {
            if (d.severity == Severity::Error) {
                failStage = QString::fromStdString(d.stage);
                break;
            }
        }
        statusLabel_->setText(tr("失败（%1）").arg(failStage));
        emit statusMessage(tr("任务二执行失败：阶段 %1，详见日志").arg(failStage));
        emit logMessage(tr("[Error] 任务二执行失败：阶段 %1").arg(failStage));
    }
}

void Task2Page::logDiagnostics(const pipeline::ParserPipelineSnapshot& snap)
{
    for (const Diagnostic& d : snap.diagnostics) {
        emit diagnostic(d);
    }
}

void Task2Page::applySnapshot(const pipeline::ParserPipelineSnapshot& snap)
{
    using pipeline::ParseStage;
    const int valid = static_cast<int>(snap.validThrough);
    const int kSimplify = static_cast<int>(ParseStage::Simplify);
    const int kFirst = static_cast<int>(ParseStage::FirstFollow);
    const int kLr0 = static_cast<int>(ParseStage::Lr0);
    const int kLalr = static_cast<int>(ParseStage::Lalr1);
    const int kTable = static_cast<int>(ParseStage::ParseTable);
    const int kDrive = static_cast<int>(ParseStage::Drive);
    const int kIr = static_cast<int>(ParseStage::Ir);

    // Tab1 BNF：编辑器持有（可编辑），不回填

    if (valid >= kSimplify) {
        simplifiedView_->setPlainText(renderSimplifiedText(snap));
    } else {
        simplifiedView_->clear();
    }

    if (valid >= kFirst) {
        populateFirstFollowTable(firstFollowTable_, snap.simplifiedGrammar, snap.firstFollow);
    } else {
        firstFollowTable_->setRowCount(0);
    }

    if (valid >= kLr0) {
        populateLr0Table(lr0Table_, snap.simplifiedGrammar, snap.lr0);
    } else {
        lr0Table_->setRowCount(0);
    }
    if (valid >= kLalr) {
        populateLalr1Table(lalr1Table_, snap.simplifiedGrammar, snap.lalr1);
    } else {
        lalr1Table_->setRowCount(0);
    }

    if (valid >= kTable) {
        populateParseTable(parseTable_, snap.simplifiedGrammar, snap.parseTable);
        conflictView_->setPlainText(
            renderConflictReport(snap.simplifiedGrammar, snap.parseTable));
    } else {
        parseTable_->setRowCount(0);
        conflictView_->clear();
    }

    if (valid >= kDrive && snap.parseTree) {
        populateParseTree(parseTreeView_, *snap.parseTree, treeStatsLabel_);
    } else {
        parseTreeView_->clear();
        treeStatsLabel_->setText(tr("语法树：空"));
    }
    if (valid >= kIr) {
        irView_->setPlainText(QString::fromStdString(pipeline::formatIrProgram(snap.ir)));
    } else {
        irView_->clear();
    }
}

void Task2Page::clearArtifactTabs()
{
    simplifiedView_->clear();
    firstFollowTable_->setRowCount(0);
    lr0Table_->setRowCount(0);
    lalr1Table_->setRowCount(0);
    parseTable_->setRowCount(0);
    conflictView_->clear();
    parseTreeView_->clear();
    treeStatsLabel_->setText(tr("语法树：空"));
    irView_->clear();
}

} // namespace compiler::gui
