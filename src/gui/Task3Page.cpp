#include "Task3Page.h"

#include <string>
#include <vector>

#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QList>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QThread>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "Diagnostic.h"
#include "ParseTreeView.h"
#include "ParserPipeline.h"
#include "Task3PipelineWorker.h"
#include "Token.h"

namespace compiler::gui {
namespace {

QString kindTextQt(TokenKind kind)
{
    switch (kind) {
    case TokenKind::EndOfFile:  return QStringLiteral("EOF");
    case TokenKind::Keyword:    return QStringLiteral("Keyword");
    case TokenKind::Identifier: return QStringLiteral("Identifier");
    case TokenKind::Number:     return QStringLiteral("Number");
    case TokenKind::String:     return QStringLiteral("String");
    case TokenKind::Operator:   return QStringLiteral("Operator");
    case TokenKind::Punct:      return QStringLiteral("Punct");
    case TokenKind::Comment:    return QStringLiteral("Comment");
    case TokenKind::Error:      return QStringLiteral("Error");
    case TokenKind::Unknown:    return QStringLiteral("Unknown");
    }
    return QStringLiteral("Unknown");
}

QTableWidgetItem* readOnlyItem(const QString& text)
{
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

// Tab2：Token 表（code/name/kind/lexeme/顺序；sample.lex 不含源程序行列号）
void populateTokenTable(QTableWidget* table, const std::vector<Token>& tokens)
{
    table->clear();
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels({QStringLiteral("#"), QStringLiteral("code"),
                                      QStringLiteral("name"), QStringLiteral("类别"),
                                      QStringLiteral("词素"), QStringLiteral("顺序")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setRowCount(static_cast<int>(tokens.size()));

    const QBrush errorBrush(QColor(QStringLiteral("#d9534f")));
    for (size_t i = 0; i < tokens.size(); ++i) {
        const Token& t = tokens[i];
        const int row = static_cast<int>(i);
        table->setItem(row, 0, readOnlyItem(QString::number(row + 1)));
        table->setItem(row, 1, readOnlyItem(QString::number(t.code)));
        table->setItem(row, 2, readOnlyItem(QString::fromStdString(t.name)));
        table->setItem(row, 3, readOnlyItem(kindTextQt(t.kind)));
        table->setItem(row, 4, readOnlyItem(QString::fromStdString(t.lexeme)));
        table->setItem(row, 5, readOnlyItem(QString::number(t.loc.line)));
        if (t.isError()) {
            for (int c = 0; c < 6; ++c) {
                table->item(row, c)->setForeground(errorBrush);
            }
        }
    }
}

// Tab1：覆盖矩阵（keyword / symbol / production 三段合并为一张表）
void populateCoverageTable(QTableWidget* table, const JointCoverage& coverage)
{
    table->clear();
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels({QStringLiteral("类别"), QStringLiteral("项目"),
                                      QStringLiteral("覆盖")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    const int total = static_cast<int>(coverage.keywords.size() + coverage.symbols.size()
                                       + coverage.productions.size());
    table->setRowCount(total);

    const QBrush okBrush(QColor(QStringLiteral("#2e7d32")));
    const QBrush missBrush(QColor(QStringLiteral("#d9534f")));
    const QBrush epsBrush(QColor(QStringLiteral("#888888")));

    int row = 0;
    const auto appendGroup = [&](const QString& category,
                                 const std::vector<CoverageItem>& items) {
        for (size_t i = 0; i < items.size(); ++i) {
            const CoverageItem& it = items[i];
            table->setItem(row, 0,
                           readOnlyItem(i == 0 ? category : QString()));
            table->setItem(row, 1, readOnlyItem(QString::fromStdString(it.label)));
            QString status = it.epsilon ? QStringLiteral("ε（无树节点）")
                                        : (it.covered ? QStringLiteral("✓")
                                                      : QStringLiteral("—"));
            auto* statusItem = readOnlyItem(status);
            statusItem->setForeground(it.epsilon ? epsBrush
                                                 : (it.covered ? okBrush : missBrush));
            if (!it.epsilon && !it.covered) {
                QFont f = statusItem->font();
                f.setBold(true);
                statusItem->setFont(f);
            }
            table->setItem(row, 2, statusItem);
            ++row;
        }
    };
    appendGroup(QStringLiteral("关键字"), coverage.keywords);
    appendGroup(QStringLiteral("符号"), coverage.symbols);
    appendGroup(QStringLiteral("产生式"), coverage.productions);
}

QFont fixedFont()
{
    return QFontDatabase::systemFont(QFontDatabase::FixedFont);
}

} // namespace

Task3Page::Task3Page(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);

    createToolbar();
    createTabs();

    layout->addWidget(toolbar_, 0);
    layout->addWidget(tabs_, 1);

    // ---- 联合流水线工作线程（同 T1.9/T2.10 模式）----
    worker_ = new Task3PipelineWorker;
    workerThread_ = new QThread;   // 无父对象，析构显式按序删除
    worker_->moveToThread(workerThread_);

    connect(worker_, &Task3PipelineWorker::stageStarted,
            this, &Task3Page::onStageStarted);
    connect(worker_, &Task3PipelineWorker::logMessage, this, &Task3Page::logMessage);
    connect(worker_, &Task3PipelineWorker::finished, this, &Task3Page::onFinished);

    workerThread_->start();
}

Task3Page::~Task3Page()
{
    // 运行中关闭窗口不崩溃：先请求停止（阶段边界生效），再退出并等待线程
    if (workerThread_) {
        worker_->requestStop();
        workerThread_->quit();
        workerThread_->wait();
    }
    delete worker_;
    delete workerThread_;
}

// ---- 操作栏 ----

void Task3Page::createToolbar()
{
    toolbar_ = new QWidget(this);
    auto* root = new QVBoxLayout(toolbar_);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);

    auto* row1 = new QHBoxLayout;
    openPackBtn_ = new QPushButton(tr("打开语言包目录"), toolbar_);
    openSourceBtn_ = new QPushButton(tr("打开源程序"), toolbar_);
    saveSourceBtn_ = new QPushButton(tr("保存源程序"), toolbar_);
    runJointBtn_ = new QPushButton(tr("一键联合测试"), toolbar_);
    runJointBtn_->setDefault(true);
    runLexerBtn_ = new QPushButton(tr("仅词法测试"), toolbar_);
    runParserBtn_ = new QPushButton(tr("仅语法测试"), toolbar_);
    presetBtn_ = new QPushButton(tr("载入预置 sample.lex"), toolbar_);
    presetBtn_->setToolTip(tr("独立发布环境（未安装 g++）时使用：直接载入 generated/<语言>/sample.lex，"
                              "跳过生成扫描器的编译与运行，语法与中间代码阶段照常执行"));
    exportBtn_ = new QPushButton(tr("导出测试报告"), toolbar_);

    connect(openPackBtn_, &QPushButton::clicked, this, &Task3Page::openLanguagePack);
    connect(openSourceBtn_, &QPushButton::clicked, this, &Task3Page::openSourceFile);
    connect(saveSourceBtn_, &QPushButton::clicked, this, &Task3Page::saveSourceFile);
    connect(runJointBtn_, &QPushButton::clicked, this, &Task3Page::runJoint);
    connect(runLexerBtn_, &QPushButton::clicked, this, &Task3Page::runLexerOnly);
    connect(runParserBtn_, &QPushButton::clicked, this, &Task3Page::runParserOnly);
    connect(presetBtn_, &QPushButton::clicked, this, &Task3Page::runPreset);
    connect(exportBtn_, &QPushButton::clicked, this, &Task3Page::exportReport);

    row1->addWidget(openPackBtn_);
    row1->addWidget(openSourceBtn_);
    row1->addWidget(saveSourceBtn_);
    row1->addSpacing(12);
    row1->addWidget(runJointBtn_);
    row1->addWidget(runLexerBtn_);
    row1->addWidget(runParserBtn_);
    row1->addWidget(presetBtn_);
    row1->addSpacing(12);
    row1->addWidget(exportBtn_);
    row1->addStretch();
    root->addLayout(row1);

    auto* row2 = new QHBoxLayout;
    toTask1Btn_ = new QPushButton(tr("查看任务一词法中间产物"), toolbar_);
    toTask2Btn_ = new QPushButton(tr("查看任务二完整分析表"), toolbar_);
    toTask1Btn_->setToolTip(tr("切换到「任务一 · 词法分析」Tab（不复制其内部状态）"));
    toTask2Btn_->setToolTip(tr("切换到「任务二 · 语法分析」Tab（不复制其内部状态）"));
    connect(toTask1Btn_, &QPushButton::clicked, this, &Task3Page::requestTask1);
    connect(toTask2Btn_, &QPushButton::clicked, this, &Task3Page::requestTask2);
    row2->addWidget(new QLabel(tr("跳转："), toolbar_));
    row2->addWidget(toTask1Btn_);
    row2->addWidget(toTask2Btn_);
    row2->addStretch();
    root->addLayout(row2);

    auto* row3 = new QHBoxLayout;
    packLabel_ = new QLabel(tr("语言包：未加载"), toolbar_);
    sourceLabel_ = new QLabel(tr("源程序：未打开"), toolbar_);
    outputLabel_ = new QLabel(tr("输出：%1").arg(outputDir()), toolbar_);
    elapsedLabel_ = new QLabel(tr("耗时：—"), toolbar_);
    statusLabel_ = new QLabel(tr("就绪"), toolbar_);
    for (QLabel* label : {packLabel_, sourceLabel_, outputLabel_}) {
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    }
    statusLabel_->setStyleSheet(QStringLiteral("font-weight:bold;"));
    row3->addWidget(packLabel_);
    row3->addSpacing(16);
    row3->addWidget(sourceLabel_);
    row3->addSpacing(16);
    row3->addWidget(outputLabel_);
    row3->addStretch();
    row3->addWidget(elapsedLabel_);
    row3->addSpacing(16);
    row3->addWidget(statusLabel_);
    root->addLayout(row3);
}

// ---- 4 个 Tab ----

void Task3Page::createTabs()
{
    tabs_ = new QTabWidget(this);
    createSourceTab(tabs_);
    createLexerTab(tabs_);
    createParserTab(tabs_);
    createReportTab(tabs_);
}

void Task3Page::createSourceTab(QTabWidget* tabs)
{
    auto* tab = new QWidget(tabs);
    auto* layout = new QVBoxLayout(tab);

    coverageSummaryLabel_ = new QLabel(
        tr("覆盖：—（执行「一键联合测试」后计算关键字 / 符号 / 产生式覆盖）"), tab);
    coverageSummaryLabel_->setWordWrap(true);

    auto* split = new QSplitter(Qt::Horizontal, tab);

    sourceEditor_ = new QPlainTextEdit(split);
    sourceEditor_->setFont(fixedFont());
    sourceEditor_->setLineWrapMode(QPlainTextEdit::NoWrap);
    sourceEditor_->setPlaceholderText(
        tr("Mini-C 源程序。可「打开源程序」载入 samples/test.minic，或直接粘贴后运行"));
    connect(sourceEditor_, &QPlainTextEdit::textChanged,
            this, &Task3Page::onSourceModified);

    auto* right = new QWidget(split);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(new QLabel(tr("覆盖检查（未覆盖项标红）"), right));
    coverageTable_ = new QTableWidget(right);
    rightLayout->addWidget(coverageTable_, 1);

    split->addWidget(sourceEditor_);
    split->addWidget(right);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    split->setSizes({640, 420});

    layout->addWidget(coverageSummaryLabel_, 0);
    layout->addWidget(split, 1);
    tabs->addTab(tab, tr("源程序"));
}

void Task3Page::createLexerTab(QTabWidget* tabs)
{
    auto* tab = new QWidget(tabs);
    auto* layout = new QVBoxLayout(tab);

    auto* head = new QHBoxLayout;
    lexStatsLabel_ = new QLabel(tr("Token：—     ERROR：—"), tab);
    lexBadgeLabel_ = new QLabel(tr("尚未运行"), tab);
    lexBadgeLabel_->setStyleSheet(QStringLiteral("font-weight:bold;"));
    head->addWidget(lexStatsLabel_);
    head->addStretch();
    head->addWidget(lexBadgeLabel_);
    layout->addLayout(head);

    auto* split = new QSplitter(Qt::Vertical, tab);

    tokenTable_ = new QTableWidget(split);
    tokenTable_->setAlternatingRowColors(true);

    auto* lexPane = new QWidget(split);
    auto* lexLayout = new QVBoxLayout(lexPane);
    lexLayout->setContentsMargins(0, 0, 0, 0);
    auto* hint = new QLabel(
        tr("sample.lex（真实生成扫描器产出；sample.lex 不含源程序行列号，"
           "Token 表「顺序」为数据区序号）"), lexPane);
    hint->setWordWrap(true);
    sampleLexView_ = new QPlainTextEdit(lexPane);
    sampleLexView_->setReadOnly(true);
    sampleLexView_->setFont(fixedFont());
    sampleLexView_->setLineWrapMode(QPlainTextEdit::NoWrap);
    sampleLexView_->setPlaceholderText(tr("sample.lex 内容"));
    lexLayout->addWidget(hint, 0);
    lexLayout->addWidget(sampleLexView_, 1);

    split->addWidget(tokenTable_);
    split->addWidget(lexPane);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    split->setSizes({420, 260});

    layout->addWidget(split, 1);
    tabs->addTab(tab, tr("词法结果"));
}

void Task3Page::createParserTab(QTabWidget* tabs)
{
    auto* tab = new QWidget(tabs);
    auto* layout = new QVBoxLayout(tab);

    syntaxStatsLabel_ = new QLabel(
        tr("文法：—    LR(0)：—    LALR(1)：—    冲突：—    IR：—"), tab);
    syntaxStatsLabel_->setWordWrap(true);
    layout->addWidget(syntaxStatsLabel_, 0);

    auto* split = new QSplitter(Qt::Vertical, tab);

    auto* treePane = new QWidget(split);
    auto* treeLayout = new QVBoxLayout(treePane);
    treeLayout->setContentsMargins(0, 0, 0, 0);
    treeStatsLabel_ = new QLabel(tr("语法树：空"), treePane);
    parseTreeView_ = new QTreeWidget(treePane);
    parseTreeView_->setAlternatingRowColors(true);
    treeLayout->addWidget(treeStatsLabel_, 0);
    treeLayout->addWidget(parseTreeView_, 1);

    auto* irPane = new QWidget(split);
    auto* irLayout = new QVBoxLayout(irPane);
    irLayout->setContentsMargins(0, 0, 0, 0);
    irLayout->addWidget(new QLabel(tr("四元式中间代码（IR）"), irPane));
    irView_ = new QPlainTextEdit(irPane);
    irView_->setReadOnly(true);
    irView_->setFont(fixedFont());
    irView_->setLineWrapMode(QPlainTextEdit::NoWrap);
    irView_->setPlaceholderText(tr("四元式 (op, arg1, arg2, result)"));
    irLayout->addWidget(irView_, 1);

    split->addWidget(treePane);
    split->addWidget(irPane);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    split->setSizes({430, 260});

    layout->addWidget(split, 1);
    tabs->addTab(tab, tr("语法结果"));
}

void Task3Page::createReportTab(QTabWidget* tabs)
{
    auto* tab = new QWidget(tabs);
    auto* layout = new QVBoxLayout(tab);

    reportView_ = new QPlainTextEdit(tab);
    reportView_->setReadOnly(true);
    reportView_->setFont(fixedFont());
    reportView_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    reportView_->setPlaceholderText(
        tr("联合测试报告（Markdown）。执行测试后自动生成，可「导出测试报告」保存。"));

    layout->addWidget(reportView_, 1);
    tabs->addTab(tab, tr("联合报告"));
}

// ---- 输入 ----

QString Task3Page::outputDir() const
{
    // 两种布局（与 Task1Page::outputDir 保持一致）：
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

bool Task3Page::readTextFile(const QString& path, QString& out) const
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    out = QString::fromUtf8(f.readAll());
    return true;
}

bool Task3Page::loadLanguagePack(const QString& dir)
{
    const QDir d(dir);
    const QString base = d.dirName();
    QString rulesPath = d.filePath(base + QStringLiteral(".rules"));
    QString bnfPath = d.filePath(base + QStringLiteral(".bnf"));

    if (!QFileInfo::exists(rulesPath)) {
        const QStringList candidates = d.entryList({QStringLiteral("*.rules")}, QDir::Files);
        if (candidates.isEmpty()) {
            emit logMessage(tr("[Error] [input] 语言包缺少 .rules 文件：%1").arg(dir));
            return false;
        }
        rulesPath = d.filePath(candidates.first());
    }
    if (!QFileInfo::exists(bnfPath)) {
        const QStringList candidates = d.entryList({QStringLiteral("*.bnf")}, QDir::Files);
        if (candidates.isEmpty()) {
            emit logMessage(tr("[Error] [input] 语言包缺少 .bnf 文件：%1").arg(dir));
            return false;
        }
        bnfPath = d.filePath(candidates.first());
    }

    QString rules;
    QString bnf;
    if (!readTextFile(rulesPath, rules)) {
        emit logMessage(tr("[Error] [input] 无法读取规则文件：%1").arg(rulesPath));
        return false;
    }
    if (!readTextFile(bnfPath, bnf)) {
        emit logMessage(tr("[Error] [input] 无法读取文法文件：%1").arg(bnfPath));
        return false;
    }

    packDir_ = dir;
    rulesPath_ = rulesPath;
    bnfPath_ = bnfPath;
    rulesText_ = rules;
    bnfText_ = bnf;

    packLabel_->setText(tr("语言包：%1（%2 + %3）")
                            .arg(dir, QFileInfo(rulesPath).fileName(),
                                 QFileInfo(bnfPath).fileName()));
    clearArtifacts();
    emit logMessage(tr("[Info] 已加载语言包：%1").arg(dir));
    return true;
}

bool Task3Page::reloadLanguagePack(QString& rulesText, QString& bnfText)
{
    if (packDir_.isEmpty() || rulesPath_.isEmpty() || bnfPath_.isEmpty()) {
        emit logMessage(tr("[Error] [input] 请先打开语言包目录（含 .rules / .bnf）"));
        return false;
    }
    if (!readTextFile(rulesPath_, rulesText)) {
        emit logMessage(tr("[Error] [input] 无法读取规则文件：%1").arg(rulesPath_));
        return false;
    }
    if (!readTextFile(bnfPath_, bnfText)) {
        emit logMessage(tr("[Error] [input] 无法读取文法文件：%1").arg(bnfPath_));
        return false;
    }
    rulesText_ = rulesText;
    bnfText_ = bnfText;
    return true;
}

void Task3Page::openLanguagePack()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("打开语言包目录（含 <lang>.rules 与 <lang>.bnf）"),
        packDir_.isEmpty() ? QString() : packDir_,
        QFileDialog::ShowDirsOnly);
    if (dir.isEmpty()) {
        return;
    }
    if (loadLanguagePack(dir)) {
        statusLabel_->setText(tr("语言包就绪"));
        emit statusMessage(tr("任务三：语言包 %1").arg(dir));
    }
}

void Task3Page::openSourceFile()
{
    const QString startDir = packDir_.isEmpty()
        ? QString()
        : QDir(packDir_).filePath(QStringLiteral("samples"));
    const QString path = QFileDialog::getOpenFileName(
        this, tr("打开 Mini-C 源程序"), startDir,
        tr("源程序 (*.minic *.c *.txt);;所有文件 (*)"));
    if (path.isEmpty()) {
        return;
    }
    QString text;
    if (!readTextFile(path, text)) {
        emit logMessage(tr("[Error] [input] 无法打开源程序：%1").arg(path));
        return;
    }
    sourceEditor_->setPlainText(text);
    sourcePath_ = path;
    sourceDirty_ = false;   // setPlainText 触发 textChanged，这里覆盖回来
    sourceLabel_->setText(tr("源程序：%1").arg(path));
    emit logMessage(tr("[Info] 已加载源程序：%1").arg(path));
}

void Task3Page::saveSourceFile()
{
    QString path = sourcePath_;
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(
            this, tr("保存 Mini-C 源程序"),
            packDir_.isEmpty() ? QStringLiteral("test.minic")
                               : QDir(packDir_).filePath(QStringLiteral("samples/test.minic")),
            tr("Mini-C 源程序 (*.minic);;所有文件 (*)"));
        if (path.isEmpty()) {
            return;
        }
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit logMessage(tr("[Error] [input] 无法保存源程序：%1").arg(path));
        return;
    }
    f.write(sourceEditor_->toPlainText().toUtf8());
    f.close();
    sourcePath_ = path;
    sourceDirty_ = false;
    sourceLabel_->setText(tr("源程序：%1").arg(path));
    emit logMessage(tr("[Info] 已保存源程序：%1").arg(path));
}

void Task3Page::onSourceModified()
{
    sourceDirty_ = true;
    if (sourcePath_.isEmpty()) {
        sourceLabel_->setText(tr("源程序：未保存的编辑内容"));
    } else {
        sourceLabel_->setText(tr("源程序：%1（已修改）").arg(sourcePath_));
    }
}

// ---- 运行 ----

void Task3Page::runJoint()
{
    startRun(static_cast<int>(JointRunMode::Joint),
             static_cast<int>(pipeline::JointStage::Ir));
}

void Task3Page::runLexerOnly()
{
    startRun(static_cast<int>(JointRunMode::Lexer),
             static_cast<int>(pipeline::Stage::Run));
}

void Task3Page::runParserOnly()
{
    startRun(static_cast<int>(JointRunMode::Parser),
             static_cast<int>(pipeline::ParseStage::Ir));
}

void Task3Page::runPreset()
{
    startRun(static_cast<int>(JointRunMode::Preset), 0);
}

void Task3Page::startRun(int mode, int stage)
{
    if (busy_) {
        return;
    }

    QString rulesText;
    QString bnfText;
    if (!reloadLanguagePack(rulesText, bnfText)) {
        return;
    }
    const QString sourceText = sourceEditor_->toPlainText();
    if (sourceText.trimmed().isEmpty()) {
        emit logMessage(tr("[Error] [input] 源程序为空，请先打开或输入 Mini-C 源程序"));
        return;
    }

    // 仅语法测试：依赖同一 worker 内上一轮真实产出的 sample.lex（严格校验）
    if (mode == static_cast<int>(JointRunMode::Parser)) {
        if (!lastSnapshot_.strictLexOk) {
            emit logMessage(tr("[Error] [joint-parse] 仅语法测试需要先执行「仅词法测试」"
                               "或「一键联合测试」，且词法必须由生成扫描器真实产出"));
            return;
        }
        if (lastRunSource_ != sourceText) {
            emit logMessage(tr("[Error] [joint-parse] 源程序已修改，现有 sample.lex 已过期；"
                               "请重新执行「仅词法测试」或「一键联合测试」"));
            return;
        }
    }

    lastRunSource_ = sourceText;

    setBusy(true);
    statusLabel_->setText(tr("执行中…"));
    emit statusMessage(tr("任务三：执行中…"));

    QMetaObject::invokeMethod(
        worker_, "runTask", Qt::QueuedConnection,
        Q_ARG(int, mode),
        Q_ARG(int, stage),
        Q_ARG(QString, rulesText),
        Q_ARG(QString, rulesPath_),
        Q_ARG(QString, bnfText),
        Q_ARG(QString, bnfPath_),
        Q_ARG(QString, sourceText),
        Q_ARG(QString, outputDir()));
}

void Task3Page::setBusy(bool busy)
{
    busy_ = busy;
    const QList<QPushButton*> buttons = {
        openPackBtn_, openSourceBtn_, saveSourceBtn_, runJointBtn_,
        runLexerBtn_, runParserBtn_, presetBtn_, exportBtn_,
    };
    for (QPushButton* b : buttons) {
        if (b) {
            b->setEnabled(!busy);
        }
    }
    if (!busy && statusLabel_) {
        statusLabel_->setText(tr("就绪"));
    }
}

void Task3Page::onStageStarted(const QString& group, const QString& name)
{
    statusLabel_->setText(tr("正在：%1 · %2…").arg(group, name));
    emit logMessage(tr("[Info] [pipeline] %1 · %2").arg(group, name));
}

void Task3Page::onFinished(bool ok, const pipeline::JointSnapshot& snapshot,
                           qint64 elapsedMs)
{
    setBusy(false);
    hasRun_ = true;
    lastSnapshot_ = snapshot;
    lastElapsedMs_ = elapsedMs;

    logDiagnostics(snapshot);
    applySnapshot(snapshot, elapsedMs);

    elapsedLabel_->setText(tr("耗时：%1 ms").arg(elapsedMs));

    if (ok) {
        statusLabel_->setText(tr("完成"));
        emit statusMessage(tr("任务三联合测试成功（%1 ms）").arg(elapsedMs));
        emit logMessage(tr("[Info] 任务三执行成功：阶段 %1，耗时 %2 ms")
                            .arg(QString::fromUtf8(pipeline::jointStageName(snapshot.validThrough)))
                            .arg(elapsedMs));
    } else {
        const QString stage = firstErrorStage(snapshot);
        statusLabel_->setText(tr("失败（%1）").arg(stage));
        emit statusMessage(tr("任务三执行失败：阶段 %1，详见日志").arg(stage));
        emit logMessage(tr("[Error] 任务三执行失败：阶段 %1").arg(stage));
    }
}

QString Task3Page::firstErrorStage(const pipeline::JointSnapshot& snapshot) const
{
    for (const Diagnostic& d : snapshot.diagnostics) {
        if (d.severity == Severity::Error) {
            return QString::fromStdString(d.stage);
        }
    }
    return QStringLiteral("未知");
}

void Task3Page::logDiagnostics(const pipeline::JointSnapshot& snapshot)
{
    for (const Diagnostic& d : snapshot.diagnostics) {
        emit diagnostic(d);
    }
}

// ---- 结果展示 ----

void Task3Page::applySnapshot(const pipeline::JointSnapshot& snapshot, qint64 elapsedMs)
{
    Q_UNUSED(elapsedMs);

    // ---- Tab2 词法结果 ----
    const std::vector<Token> tokens = jointTokens(snapshot);
    populateTokenTable(tokenTable_, tokens);

    int errorCount = 0;
    for (const Token& t : tokens) {
        if (t.isError()) {
            ++errorCount;
        }
    }
    lexStatsLabel_->setText(
        tr("Token：%1     ERROR：%2     登记项：%3")
            .arg(static_cast<int>(tokens.size()))
            .arg(errorCount)
            .arg(static_cast<int>(snapshot.parse.registry.orderedTypes().size())));

    if (snapshot.presetLex) {
        lexBadgeLabel_->setText(tr("◎ 预置产物（本机未运行生成扫描器，可继续语法分析）"));
        lexBadgeLabel_->setStyleSheet(QStringLiteral("color:#b26a00;font-weight:bold;"));
        sampleLexView_->setPlainText(QString::fromStdString(snapshot.lex.runLexText));
    } else if (snapshot.runProducedLex) {
        lexBadgeLabel_->setText(tr("✓ 真实生成扫描器产出"));
        lexBadgeLabel_->setStyleSheet(QStringLiteral("color:#2e7d32;font-weight:bold;"));
        sampleLexView_->setPlainText(QString::fromStdString(snapshot.lex.runLexText));
    } else if (!snapshot.lex.previewLexText.empty()) {
        lexBadgeLabel_->setText(tr("✗ 进程内预览（未进入语法分析）"));
        lexBadgeLabel_->setStyleSheet(QStringLiteral("color:#d9534f;font-weight:bold;"));
        sampleLexView_->setPlainText(QString::fromStdString(snapshot.lex.previewLexText));
    } else {
        lexBadgeLabel_->setText(tr("未产出 sample.lex"));
        lexBadgeLabel_->setStyleSheet(QStringLiteral("color:#888888;font-weight:bold;"));
        sampleLexView_->clear();
    }
    if (!snapshot.sampleLexPath.empty()) {
        lexStatsLabel_->setToolTip(
            tr("sample.lex：%1\n生成扫描器：%2")
                .arg(QString::fromStdString(snapshot.sampleLexPath),
                     QString::fromStdString(snapshot.lex.exePath)));
    }

    // ---- Tab1 覆盖矩阵 ----
    const JointCoverage coverage = computeJointCoverage(snapshot);
    populateCoverageTable(coverageTable_, coverage);
    coverageSummaryLabel_->setText(
        tr("覆盖：关键字 %1 ｜ 符号 %2 ｜ 产生式 %3")
            .arg(QString::fromStdString(coverageSummary(coverage.keywords)),
                 QString::fromStdString(coverageSummary(coverage.symbols)),
                 QString::fromStdString(coverageSummary(coverage.productions))));

    // ---- Tab3 语法结果 ----
    const pipeline::ParserPipelineSnapshot& parse = snapshot.parse;
    const int quadCount = static_cast<int>(parse.ir.quads.size());
    syntaxStatsLabel_->setText(
        tr("文法：%1 条产生式    LR(0)：%2 态    LALR(1)：%3 态    冲突：%4    IR：%5 条")
            .arg(static_cast<int>(parse.simplifiedGrammar.productions.size()))
            .arg(static_cast<int>(parse.lr0.states.size()))
            .arg(static_cast<int>(parse.lalr1.states.size()))
            .arg(static_cast<int>(parse.parseTable.conflicts.size()))
            .arg(quadCount));

    if (parse.parseTree) {
        populateParseTree(parseTreeView_, *parse.parseTree, treeStatsLabel_);
    } else {
        parseTreeView_->clear();
        treeStatsLabel_->setText(tr("语法树：空"));
    }

    if (quadCount > 0) {
        irView_->setPlainText(
            QString::fromStdString(pipeline::formatIrProgram(parse.ir)));
    } else {
        irView_->clear();
    }

    // ---- Tab4 联合报告 ----
    JointReportContext context;
    context.rulesPath = rulesPath_.toStdString();
    context.bnfPath = bnfPath_.toStdString();
    context.sourcePath = reportContextSourcePath().toStdString();
    context.outputDir = outputDir().toStdString();
    reportView_->setPlainText(
        QString::fromStdString(formatJointReportMarkdown(snapshot, context)));
}

void Task3Page::clearArtifacts()
{
    // 语言包变更：清空本轮展示（下次运行按快照重填）
    hasRun_ = false;
    lastSnapshot_ = pipeline::JointSnapshot{};
    lastRunSource_.clear();
    lastElapsedMs_ = 0;
    if (tokenTable_) {
        tokenTable_->setRowCount(0);
    }
    if (sampleLexView_) {
        sampleLexView_->clear();
    }
    if (lexStatsLabel_) {
        lexStatsLabel_->setText(tr("Token：—     ERROR：—"));
    }
    if (lexBadgeLabel_) {
        lexBadgeLabel_->setText(tr("尚未运行"));
        lexBadgeLabel_->setStyleSheet(QStringLiteral("font-weight:bold;"));
    }
    if (coverageTable_) {
        coverageTable_->setRowCount(0);
    }
    if (coverageSummaryLabel_) {
        coverageSummaryLabel_->setText(
            tr("覆盖：—（执行「一键联合测试」后计算关键字 / 符号 / 产生式覆盖）"));
    }
    if (parseTreeView_) {
        parseTreeView_->clear();
    }
    if (treeStatsLabel_) {
        treeStatsLabel_->setText(tr("语法树：空"));
    }
    if (irView_) {
        irView_->clear();
    }
    if (syntaxStatsLabel_) {
        syntaxStatsLabel_->setText(
            tr("文法：—    LR(0)：—    LALR(1)：—    冲突：—    IR：—"));
    }
    if (reportView_) {
        reportView_->clear();
    }
    if (elapsedLabel_) {
        elapsedLabel_->setText(tr("耗时：—"));
    }
}

// ---- 报告导出 ----

QString Task3Page::reportContextSourcePath() const
{
    if (sourcePath_.isEmpty()) {
        return tr("（编辑器内容，未保存到文件）");
    }
    if (sourceDirty_) {
        return tr("%1（编辑器内容已修改，流水线以缓冲区落盘 _input_src.txt）")
            .arg(sourcePath_);
    }
    return sourcePath_;
}

QString Task3Page::defaultReportName() const
{
    if (!sourcePath_.isEmpty()) {
        return QFileInfo(sourcePath_).fileName() + QStringLiteral(".report.md");
    }
    return QStringLiteral("minic.report.md");
}

void Task3Page::exportReport()
{
    if (!hasRun_ || reportView_->toPlainText().isEmpty()) {
        emit logMessage(tr("[Error] [report] 尚无测试结果，请先执行「一键联合测试」"));
        return;
    }

    const QString suggested = QDir(outputDir()).filePath(defaultReportName());
    const QString path = QFileDialog::getSaveFileName(
        this, tr("导出联合测试报告"), suggested,
        tr("Markdown (*.md);;所有文件 (*)"));
    if (path.isEmpty()) {
        return;
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit logMessage(tr("[Error] [report] 无法写入报告：%1").arg(path));
        QMessageBox::warning(this, tr("导出失败"), tr("无法写入文件：%1").arg(path));
        return;
    }
    f.write(reportView_->toPlainText().toUtf8());
    f.close();

    emit logMessage(tr("[Info] 测试报告已导出：%1").arg(path));
    emit statusMessage(tr("任务三：报告已导出到 %1").arg(path));
}

} // namespace compiler::gui
