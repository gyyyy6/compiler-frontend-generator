#pragma once

// 任务一 · 词法分析页面（T1.9 完整工作流）
// 顶部操作栏（打开 .rules / 打开源程序 / 一键运行 / 单步按钮）+
// 6 个子 Tab（正则表达式 / NFA / DFA / 最小DFA / 词法源程序 / 单词编码）。
// 流水线在独立 QThread（Task1PipelineWorker）中执行，结果经快照回传；
// 日志/状态经信号转发到 MainWindow（LogDock / StatusBar）。

#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTableWidget;
class QThread;
class QVBoxLayout;
class QWidget;

namespace compiler {
struct Diagnostic;
}

namespace compiler::pipeline {
enum class Stage : int;
struct PipelineSnapshot;
}

namespace compiler::gui {

class CompilerRunnerBackend;
class Task1PipelineWorker;

class Task1Page : public QWidget {
    Q_OBJECT

public:
    explicit Task1Page(QWidget* parent = nullptr);
    ~Task1Page() override;

signals:
    void logMessage(const QString& message);
    void diagnostic(const compiler::Diagnostic& d);
    void statusMessage(const QString& message);

private slots:
    void openRulesFile();
    void openSourceFile();
    void runAll();
    void runNfaOnly();
    void runDfaOnly();
    void runMinimizeOnly();
    void runCodegenOnly();
    void runCompileTest();

    // worker 信号（经排队连接回 GUI 线程）
    void onStageStarted(const QString& name);
    void onFinished(bool ok, const compiler::pipeline::PipelineSnapshot& snapshot);

private:
    void createToolbar();
    void createStepTabs();
    void startPipeline(compiler::pipeline::Stage target);
    void setBusy(bool busy);
    void applySnapshot(const compiler::pipeline::PipelineSnapshot& snap);
    void logDiagnostics(const compiler::pipeline::PipelineSnapshot& snap);
    void clearArtifactTabs();
    QString outputDir() const;

    // 操作栏控件
    QWidget* toolbar_ = nullptr;
    QPushButton* openRulesBtn_ = nullptr;
    QPushButton* openSourceBtn_ = nullptr;
    QPushButton* runAllBtn_ = nullptr;
    QPushButton* runNfaBtn_ = nullptr;
    QPushButton* runDfaBtn_ = nullptr;
    QPushButton* runMinimizeBtn_ = nullptr;
    QPushButton* runCodegenBtn_ = nullptr;
    QPushButton* runCompileTestBtn_ = nullptr;
    QLabel* rulesPathLabel_ = nullptr;
    QLabel* sourcePathLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    // 6 个子 Tab 控件
    QTabWidget* steps_ = nullptr;
    QPlainTextEdit* rulesEditor_ = nullptr;
    QTableWidget* nfaTable_ = nullptr;
    QTableWidget* dfaTable_ = nullptr;
    QTableWidget* minDfaTable_ = nullptr;
    QPlainTextEdit* sourceView_ = nullptr;     // generated_lexer.cpp
    QPlainTextEdit* lexView_ = nullptr;        // sample.lex
    QLabel* lexPreviewBadge_ = nullptr;        // "[进程内预览，非真实运行结果]" 标注

    // 流水线（独立线程）
    CompilerRunnerBackend* backend_ = nullptr;
    Task1PipelineWorker* worker_ = nullptr;
    QThread* workerThread_ = nullptr;

    QString rulesPath_;
    QString sourcePath_;
    bool busy_ = false;
};

} // namespace compiler::gui
