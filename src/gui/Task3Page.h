#pragma once

// 任务三 · Mini-C 联合测试页面（T3.5）
//
// 定位：联合测试控制台，不嵌入/不直接复用 Task1Page/Task2Page 页面；只复用其
//   底层能力——JointPipeline（T3.4）、CompilerRunnerBackend、populateParseTree()、
//   formatIrProgram()/formatParseTree()（经 Task3PipelineWorker 的报告函数）。
//
// 顶部操作栏：打开语言包 / 打开源程序 / 保存源程序 / 一键联合测试 /
//   仅词法测试 / 仅语法测试 / 导出测试报告 / 跳转任务一·任务二。
// 4 个 Tab：
//   1 源程序 + 覆盖检查   2 词法结果（Token 表 + sample.lex）
//   3 语法结果（语法树 + 冲突 + IR）   4 联合测试报告（Markdown）
//
// 流水线在独立 QThread（Task3PipelineWorker）执行，结果经 JointSnapshot 回传；
// 日志/诊断/状态经信号转发 MainWindow（LogDock / StatusBar）。

#include <QWidget>

#include "JointPipeline.h"

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QTabWidget;
class QTableWidget;
class QThread;
class QTreeWidget;

namespace compiler::gui {

class Task3PipelineWorker;
struct JointCoverage;

class Task3Page : public QWidget {
    Q_OBJECT

public:
    explicit Task3Page(QWidget* parent = nullptr);
    ~Task3Page() override;

signals:
    void logMessage(const QString& message);
    void diagnostic(const compiler::Diagnostic& d);
    void statusMessage(const QString& message);
    void requestTask1();   // 跳转到任务一 Tab（查看词法中间产物）
    void requestTask2();   // 跳转到任务二 Tab（查看完整分析表）

private slots:
    void openLanguagePack();
    void openSourceFile();
    void saveSourceFile();
    void runJoint();
    void runLexerOnly();
    void runParserOnly();
    // 载入发布包预置的 sample.lex（本机无 g++ 时的演示通道）
    void runPreset();
    void exportReport();

    void onStageStarted(const QString& group, const QString& name);
    void onFinished(bool ok, const compiler::pipeline::JointSnapshot& snapshot,
                    qint64 elapsedMs);
    void onSourceModified();

private:
    void createToolbar();
    void createTabs();
    void createSourceTab(QTabWidget* tabs);
    void createLexerTab(QTabWidget* tabs);
    void createParserTab(QTabWidget* tabs);
    void createReportTab(QTabWidget* tabs);

    // 读取/校验输入；每次运行前重读语言包，保证外部修改即时生效
    bool loadLanguagePack(const QString& dir);
    bool reloadLanguagePack(QString& rulesText, QString& bnfText);
    void startRun(int mode, int stage);
    void setBusy(bool busy);
    void applySnapshot(const compiler::pipeline::JointSnapshot& snapshot, qint64 elapsedMs);
    void clearArtifacts();
    void logDiagnostics(const compiler::pipeline::JointSnapshot& snapshot);
    bool readTextFile(const QString& path, QString& out) const;
    QString outputDir() const;
    QString reportContextSourcePath() const;
    QString defaultReportName() const;
    QString firstErrorStage(const compiler::pipeline::JointSnapshot& snapshot) const;

    // ---- 操作栏 ----
    QWidget* toolbar_ = nullptr;
    QPushButton* openPackBtn_ = nullptr;
    QPushButton* openSourceBtn_ = nullptr;
    QPushButton* saveSourceBtn_ = nullptr;
    QPushButton* runJointBtn_ = nullptr;
    QPushButton* runLexerBtn_ = nullptr;
    QPushButton* runParserBtn_ = nullptr;
    QPushButton* presetBtn_ = nullptr;
    QPushButton* exportBtn_ = nullptr;
    QPushButton* toTask1Btn_ = nullptr;
    QPushButton* toTask2Btn_ = nullptr;
    QLabel* packLabel_ = nullptr;
    QLabel* sourceLabel_ = nullptr;
    QLabel* outputLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* elapsedLabel_ = nullptr;

    // ---- Tab1 源程序 + 覆盖 ----
    QTabWidget* tabs_ = nullptr;
    QPlainTextEdit* sourceEditor_ = nullptr;
    QLabel* coverageSummaryLabel_ = nullptr;
    QTableWidget* coverageTable_ = nullptr;

    // ---- Tab2 词法结果 ----
    QLabel* lexStatsLabel_ = nullptr;
    QLabel* lexBadgeLabel_ = nullptr;
    QTableWidget* tokenTable_ = nullptr;
    QPlainTextEdit* sampleLexView_ = nullptr;

    // ---- Tab3 语法结果 ----
    QLabel* syntaxStatsLabel_ = nullptr;
    QLabel* treeStatsLabel_ = nullptr;
    QTreeWidget* parseTreeView_ = nullptr;
    QPlainTextEdit* irView_ = nullptr;

    // ---- Tab4 报告 ----
    QPlainTextEdit* reportView_ = nullptr;

    // ---- 流水线（独立线程）----
    Task3PipelineWorker* worker_ = nullptr;
    QThread* workerThread_ = nullptr;

    // ---- 输入与结果 ----
    QString packDir_;
    QString rulesPath_;
    QString bnfPath_;
    QString sourcePath_;
    QString rulesText_;
    QString bnfText_;
    bool sourceDirty_ = false;

    compiler::pipeline::JointSnapshot lastSnapshot_;
    QString lastRunSource_;    // 上一次运行时的源程序文本（判断 sample.lex 是否过期）
    qint64 lastElapsedMs_ = 0;
    bool hasRun_ = false;
    bool busy_ = false;
};

} // namespace compiler::gui
