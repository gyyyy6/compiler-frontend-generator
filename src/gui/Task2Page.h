#pragma once

// 任务二 · 语法分析页面（T2.10 完整工作流，仿 T1.9 Task1Page）
// 顶部操作栏（打开 .bnf / 打开 sample.lex / 一键运行 / 8 个单步按钮）+
// 6 个子 Tab（BNF 文法 / 化简后文法 / First·Follow / LR(0)+LALR(1) 项目集 /
//            分析表 ACTION/GOTO / 语法树 + 四元式 IR）。
// 流水线在独立 QThread（Task2PipelineWorker）中执行，结果经快照回传；
// 日志/状态经信号转发到 MainWindow（LogDock / StatusBar）。
// 注意：T2.10 起本页依赖任务二算法库（COMPILER_ENABLE_TASK2=ON，项目默认）。

#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTableWidget;
class QThread;
class QTreeWidget;
class QVBoxLayout;
class QWidget;

namespace compiler {
struct Diagnostic;
}

namespace compiler::pipeline {
enum class ParseStage : int;
struct ParserPipelineSnapshot;
}

namespace compiler::gui {

class Task2PipelineWorker;

class Task2Page : public QWidget {
    Q_OBJECT

public:
    explicit Task2Page(QWidget* parent = nullptr);
    ~Task2Page() override;

signals:
    void logMessage(const QString& message);
    void diagnostic(const compiler::Diagnostic& d);
    void statusMessage(const QString& message);

private slots:
    void openBnfFile();
    void openSampleLexFile();
    void runAll();
    void runBnfParseOnly();
    void runSimplifyOnly();
    void runFirstFollowOnly();
    void runLr0Only();
    void runLalr1Only();
    void runParseTableOnly();
    void runDriveOnly();
    void runIrOnly();

    // worker 信号（经排队连接回 GUI 线程）
    void onStageStarted(const QString& name);
    void onFinished(bool ok, const compiler::pipeline::ParserPipelineSnapshot& snapshot);

private:
    void createToolbar();
    void createStepTabs();
    void startPipeline(compiler::pipeline::ParseStage target);
    void setBusy(bool busy);
    void applySnapshot(const compiler::pipeline::ParserPipelineSnapshot& snap);
    void logDiagnostics(const compiler::pipeline::ParserPipelineSnapshot& snap);
    void clearArtifactTabs();

    // 操作栏控件
    QWidget* toolbar_ = nullptr;
    QPushButton* openBnfBtn_ = nullptr;
    QPushButton* openSampleLexBtn_ = nullptr;
    QPushButton* runAllBtn_ = nullptr;
    QPushButton* runBnfParseBtn_ = nullptr;
    QPushButton* runSimplifyBtn_ = nullptr;
    QPushButton* runFirstFollowBtn_ = nullptr;
    QPushButton* runLr0Btn_ = nullptr;
    QPushButton* runLalr1Btn_ = nullptr;
    QPushButton* runParseTableBtn_ = nullptr;
    QPushButton* runDriveBtn_ = nullptr;
    QPushButton* runIrBtn_ = nullptr;
    QLabel* bnfPathLabel_ = nullptr;
    QLabel* sampleLexPathLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    // 6 个子 Tab 控件
    QTabWidget* steps_ = nullptr;
    QPlainTextEdit* bnfEditor_ = nullptr;       // Tab1 BNF 文法（可编辑）
    QPlainTextEdit* simplifiedView_ = nullptr;  // Tab2 化简后文法（只读）
    QTableWidget* firstFollowTable_ = nullptr;  // Tab3 First/Follow
    QTabWidget* itemSetTabs_ = nullptr;         // Tab4 内嵌子 Tab（LR(0)/LALR(1)）
    QTableWidget* lr0Table_ = nullptr;
    QTableWidget* lalr1Table_ = nullptr;
    QTableWidget* parseTable_ = nullptr;        // Tab5 分析表
    QPlainTextEdit* conflictView_ = nullptr;    // Tab5 冲突清单（只读）
    QTreeWidget* parseTreeView_ = nullptr;      // Tab6 语法树
    QLabel* treeStatsLabel_ = nullptr;          // Tab6 统计
    QPlainTextEdit* irView_ = nullptr;          // Tab6 四元式 IR（只读）

    // 流水线（独立线程）
    Task2PipelineWorker* worker_ = nullptr;
    QThread* workerThread_ = nullptr;

    QString bnfPath_;
    QString sampleLexPath_;
    bool busy_ = false;
};

} // namespace compiler::gui
