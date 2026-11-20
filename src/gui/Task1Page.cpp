#include "Task1Page.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QThread>
#include <QVBoxLayout>

#include "AutomatonTableView.h"
#include "CompilerRunnerBackend.h"
#include "Diagnostic.h"
#include "LexerPipeline.h"
#include "Task1PipelineWorker.h"

namespace compiler::gui {
namespace {

const char* kPreviewBadgeText = "[进程内预览，非真实运行结果]";

} // namespace

Task1Page::Task1Page(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);

    createToolbar();
    createStepTabs();

    layout->addWidget(toolbar_, 0);
    layout->addWidget(steps_, 1);

    // ---- 流水线工作线程 ----
    backend_ = new CompilerRunnerBackend;
    worker_ = new Task1PipelineWorker(*backend_);
    workerThread_ = new QThread;   // 无父对象，析构显式按序删除
    worker_->moveToThread(workerThread_);

    connect(worker_, &Task1PipelineWorker::stageStarted,
            this, &Task1Page::onStageStarted);
    connect(worker_, &Task1PipelineWorker::logMessage,
            this, &Task1Page::logMessage);   // 信号转发（排队连接）
    connect(worker_, &Task1PipelineWorker::finished,
            this, &Task1Page::onFinished);

    workerThread_->start();
}

Task1Page::~Task1Page()
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
    delete backend_;
}

void Task1Page::createToolbar()
{
    toolbar_ = new QWidget(this);
    auto* root = new QVBoxLayout(toolbar_);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);

    auto* row1 = new QHBoxLayout;
    openRulesBtn_ = new QPushButton(tr("打开 .rules 文件"), toolbar_);
    openSourceBtn_ = new QPushButton(tr("打开源程序文件"), toolbar_);
    runAllBtn_ = new QPushButton(tr("一键运行"), toolbar_);
    runAllBtn_->setDefault(true);

    runNfaBtn_ = new QPushButton(tr("只生成 NFA"), toolbar_);
    runDfaBtn_ = new QPushButton(tr("只生成 DFA"), toolbar_);
    runMinimizeBtn_ = new QPushButton(tr("只最小化"), toolbar_);
    runCodegenBtn_ = new QPushButton(tr("只生成代码"), toolbar_);
    runCompileTestBtn_ = new QPushButton(tr("只编译测试"), toolbar_);

    connect(openRulesBtn_, &QPushButton::clicked, this, &Task1Page::openRulesFile);
    connect(openSourceBtn_, &QPushButton::clicked, this, &Task1Page::openSourceFile);
    connect(runAllBtn_, &QPushButton::clicked, this, &Task1Page::runAll);
    connect(runNfaBtn_, &QPushButton::clicked, this, &Task1Page::runNfaOnly);
    connect(runDfaBtn_, &QPushButton::clicked, this, &Task1Page::runDfaOnly);
    connect(runMinimizeBtn_, &QPushButton::clicked, this, &Task1Page::runMinimizeOnly);
    connect(runCodegenBtn_, &QPushButton::clicked, this, &Task1Page::runCodegenOnly);
    connect(runCompileTestBtn_, &QPushButton::clicked, this, &Task1Page::runCompileTest);

    row1->addWidget(openRulesBtn_);
    row1->addWidget(openSourceBtn_);
    row1->addSpacing(12);
    row1->addWidget(runAllBtn_);
    row1->addStretch();
    row1->addWidget(runNfaBtn_);
    row1->addWidget(runDfaBtn_);
    row1->addWidget(runMinimizeBtn_);
    row1->addWidget(runCodegenBtn_);
    row1->addWidget(runCompileTestBtn_);
    root->addLayout(row1);

    auto* row2 = new QHBoxLayout;
    rulesPathLabel_ = new QLabel(tr("规则文件：未打开"), toolbar_);
    sourcePathLabel_ = new QLabel(tr("源程序：未打开"), toolbar_);
    statusLabel_ = new QLabel(tr("就绪"), toolbar_);
    rulesPathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sourcePathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusLabel_->setStyleSheet(QStringLiteral("font-weight:bold;"));
    row2->addWidget(rulesPathLabel_);
    row2->addSpacing(24);
    row2->addWidget(sourcePathLabel_);
    row2->addStretch();
    row2->addWidget(statusLabel_);
    root->addLayout(row2);
}

void Task1Page::createStepTabs()
{
    steps_ = new QTabWidget(this);
    const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    // 1) 正则表达式（.rules 可编辑）
    auto* rulesTab = new QWidget(steps_);
    auto* rulesLayout = new QVBoxLayout(rulesTab);
    rulesEditor_ = new QPlainTextEdit(rulesTab);
    rulesEditor_->setFont(fixed);
    rulesEditor_->setLineWrapMode(QPlainTextEdit::NoWrap);
    rulesEditor_->setPlaceholderText(tr("此处显示/编辑 .rules 规则文件内容"));
    rulesLayout->addWidget(rulesEditor_, 1);
    steps_->addTab(rulesTab, tr("正则表达式"));

    // 2) NFA / 3) DFA / 4) 最小DFA：状态转换表
    nfaTable_ = new QTableWidget(steps_);
    dfaTable_ = new QTableWidget(steps_);
    minDfaTable_ = new QTableWidget(steps_);
    steps_->addTab(nfaTable_, tr("NFA"));
    steps_->addTab(dfaTable_, tr("DFA"));
    steps_->addTab(minDfaTable_, tr("最小DFA"));

    // 5) 词法源程序（generated_lexer.cpp，只读）
    auto* srcTab = new QWidget(steps_);
    auto* srcLayout = new QVBoxLayout(srcTab);
    sourceView_ = new QPlainTextEdit(srcTab);
    sourceView_->setReadOnly(true);
    sourceView_->setFont(fixed);
    sourceView_->setLineWrapMode(QPlainTextEdit::NoWrap);
    sourceView_->setPlaceholderText(tr("生成的独立 C++ 扫描器源码（generated_lexer.cpp）"));
    srcLayout->addWidget(sourceView_, 1);
    steps_->addTab(srcTab, tr("词法源程序"));

    // 6) 单词编码（sample.lex，只读；进程内回退时显示诚实标注）
    auto* lexTab = new QWidget(steps_);
    auto* lexLayout = new QVBoxLayout(lexTab);
    lexPreviewBadge_ = new QLabel(tr(kPreviewBadgeText), lexTab);
    lexPreviewBadge_->setStyleSheet(QStringLiteral("color:#c05000; font-weight:bold;"));
    lexPreviewBadge_->setVisible(false);
    lexView_ = new QPlainTextEdit(lexTab);
    lexView_->setReadOnly(true);
    lexView_->setFont(fixed);
    lexView_->setLineWrapMode(QPlainTextEdit::NoWrap);
    lexView_->setPlaceholderText(tr("运行生成扫描器产出的 sample.lex 单词编码文件"));
    lexLayout->addWidget(lexPreviewBadge_, 0);
    lexLayout->addWidget(lexView_, 1);
    steps_->addTab(lexTab, tr("单词编码"));
}

void Task1Page::openRulesFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("打开 .rules 规则文件"), QString(),
        tr("词法规则 (*.rules);;所有文件 (*)"));
    if (path.isEmpty()) {
        return;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit logMessage(tr("[Error] [input] 无法打开规则文件：%1").arg(path));
        return;
    }
    rulesPath_ = path;
    rulesPathLabel_->setText(tr("规则文件：%1").arg(path));
    rulesEditor_->setPlainText(QString::fromUtf8(f.readAll()));
    clearArtifactTabs();   // 规则变更：清空下游产物（下次运行按快照重填）
    statusLabel_->setText(tr("已加载规则"));
    emit logMessage(tr("[Info] 已加载规则文件：%1").arg(path));
}

void Task1Page::openSourceFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("打开源程序文件"), QString(),
        tr("源程序 (*.tny *.tiny *.src *.minic *.txt);;所有文件 (*)"));
    if (path.isEmpty()) {
        return;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit logMessage(tr("[Error] [input] 无法打开源程序文件：%1").arg(path));
        return;
    }
    sourcePath_ = path;
    sourcePathLabel_->setText(tr("源程序：%1").arg(path));
    emit logMessage(tr("[Info] 已加载源程序文件：%1").arg(path));
}

void Task1Page::runAll()
{
    startPipeline(pipeline::Stage::Run);
}

void Task1Page::runNfaOnly()
{
    startPipeline(pipeline::Stage::Nfa);
}

void Task1Page::runDfaOnly()
{
    startPipeline(pipeline::Stage::Dfa);
}

void Task1Page::runMinimizeOnly()
{
    startPipeline(pipeline::Stage::Minimize);
}

void Task1Page::runCodegenOnly()
{
    startPipeline(pipeline::Stage::Codegen);
}

void Task1Page::runCompileTest()
{
    startPipeline(pipeline::Stage::Run);   // 增量语义：上游已完成则只跑 Compile+Run
}

void Task1Page::startPipeline(pipeline::Stage target)
{
    if (busy_) {
        return;
    }
    if (rulesEditor_->toPlainText().trimmed().isEmpty()) {
        emit logMessage(tr("[Error] [input] 请先打开或输入 .rules 规则文件"));
        return;
    }
    if (target >= pipeline::Stage::Run && sourcePath_.isEmpty()) {
        emit logMessage(tr("[Error] [input] 请先打开源程序文件"));
        return;
    }

    setBusy(true);
    QMetaObject::invokeMethod(
        worker_, "runTask", Qt::QueuedConnection,
        Q_ARG(int, static_cast<int>(target)),
        Q_ARG(QString, rulesEditor_->toPlainText()),
        Q_ARG(QString, rulesPath_),
        Q_ARG(QString, sourcePath_),
        Q_ARG(QString, outputDir()));
}

void Task1Page::setBusy(bool busy)
{
    busy_ = busy;
    const QList<QPushButton*> buttons = {
        openRulesBtn_, openSourceBtn_, runAllBtn_, runNfaBtn_,
        runDfaBtn_, runMinimizeBtn_, runCodegenBtn_, runCompileTestBtn_,
    };
    for (QPushButton* b : buttons) {
        if (b) {
            b->setEnabled(!busy);
        }
    }
    statusLabel_->setText(busy ? tr("执行中…") : tr("就绪"));
    if (busy) {
        emit statusMessage(tr("任务一：执行中…"));
    }
}

void Task1Page::onStageStarted(const QString& name)
{
    statusLabel_->setText(tr("正在：%1…").arg(name));
    emit logMessage(tr("[Info] [pipeline] 开始 %1").arg(name));
}

void Task1Page::onFinished(bool ok, const compiler::pipeline::PipelineSnapshot& snap)
{
    setBusy(false);
    logDiagnostics(snap);
    applySnapshot(snap);

    if (ok) {
        statusLabel_->setText(tr("完成"));
        emit statusMessage(tr("任务一执行成功"));
        emit logMessage(tr("[Info] 任务一执行成功（已推进到阶段 %1）")
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
        emit statusMessage(tr("任务一执行失败：阶段 %1，详见日志").arg(failStage));
        emit logMessage(tr("[Error] 任务一执行失败：阶段 %1").arg(failStage));
    }
}

void Task1Page::logDiagnostics(const compiler::pipeline::PipelineSnapshot& snap)
{
    for (const Diagnostic& d : snap.diagnostics) {
        emit diagnostic(d);
    }
}

void Task1Page::applySnapshot(const compiler::pipeline::PipelineSnapshot& snap)
{
    const int valid = static_cast<int>(snap.validThrough);
    const int kRules = static_cast<int>(pipeline::Stage::Rules);
    const int kNfa = static_cast<int>(pipeline::Stage::Nfa);
    const int kDfa = static_cast<int>(pipeline::Stage::Dfa);
    const int kMinimize = static_cast<int>(pipeline::Stage::Minimize);
    const int kCodegen = static_cast<int>(pipeline::Stage::Codegen);

    if (valid >= kRules) {
        // 正则表达式 Tab 内容由编辑器持有（可编辑），无需回填
    }
    if (valid >= kNfa) {
        populateNfaTable(nfaTable_, snap.nfa);
    } else {
        nfaTable_->setRowCount(0);
    }
    if (valid >= kDfa) {
        populateDfaTable(dfaTable_, snap.dfa);
    } else {
        dfaTable_->setRowCount(0);
    }
    if (valid >= kMinimize) {
        populateDfaTable(minDfaTable_, snap.minimalDfa);
    } else {
        minDfaTable_->setRowCount(0);
    }
    if (valid >= kCodegen) {
        sourceView_->setPlainText(QString::fromStdString(snap.generatedSource));
    } else {
        sourceView_->clear();
    }

    // 单词编码：真实运行 > 进程内预览（诚实标注）> 空
    if (snap.runProducedLex) {
        lexView_->setPlainText(QString::fromStdString(snap.runLexText));
        lexPreviewBadge_->setVisible(false);
    } else if (!snap.previewLexText.empty()) {
        lexView_->setPlainText(QString::fromStdString(snap.previewLexText));
        lexPreviewBadge_->setVisible(true);
        emit logMessage(tr("[Warning] 单词编码为%1（编译/运行失败回退）")
                            .arg(tr(kPreviewBadgeText)));
    } else {
        lexView_->clear();
        lexPreviewBadge_->setVisible(false);
    }
}

void Task1Page::clearArtifactTabs()
{
    nfaTable_->setRowCount(0);
    dfaTable_->setRowCount(0);
    minDfaTable_->setRowCount(0);
    sourceView_->clear();
    lexView_->clear();
    lexPreviewBadge_->setVisible(false);
}

QString Task1Page::outputDir() const
{
    // 两种布局（与 Task3Page::outputDir 保持一致）：
    //   开发布局：exe 在 <build>/app，产物放 <build>/generated（上一级）
    //   发布布局：exe 与 generated/ 同级，产物放 <exe>/generated
    // 优先同级 generated（发布包预置的 sample.lex 即在此），否则沿用上一级。
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QDir beside(appDir.filePath(QStringLiteral("generated")));
    if (beside.exists()) {
        return beside.absolutePath();
    }
    const QDir parent(appDir.filePath(QStringLiteral("../generated")));
    if (parent.exists()) {
        return QDir::cleanPath(parent.absolutePath());
    }
    return beside.absolutePath();
}

} // namespace compiler::gui
