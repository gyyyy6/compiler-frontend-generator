#pragma once

// 任务一流水线工作线程（T1.9）
// 在独立 QThread 中执行 LexerPipeline（含阻塞的 g++ 编译与扫描器运行），
// 结果经快照信号回传 GUI 线程；关闭窗口时先 requestStop() 再退出线程。
//
// 生命周期（Task1Page 析构）：
//   worker_->requestStop();  thread_->quit();  thread_->wait();
//   delete worker_;          delete thread_;

#include <QObject>

#include <atomic>

#include "LexerPipeline.h"

class QString;

namespace compiler::gui {

class CompilerRunnerBackend;

// 流水线产物快照（跨线程经排队连接传递；qRegisterMetaType 见 .cpp）
class Task1PipelineWorker : public QObject {
    Q_OBJECT

public:
    explicit Task1PipelineWorker(CompilerRunnerBackend& backend,
                                 QObject* parent = nullptr);
    ~Task1PipelineWorker() override;

    // 线程安全：请求中止当前/后续阶段（阶段边界生效）
    void requestStop();
    bool stopRequested() const;

public slots:
    // 在 worker 线程执行：设置输入 → runThrough(stage) → 发 finished。
    // stage 为 pipeline::Stage 的 int 值。
    void runTask(int stage, const QString& rulesText, const QString& rulesPath,
                 const QString& sourcePath, const QString& outputDir);

signals:
    void stageStarted(const QString& name);
    void logMessage(const QString& message);
    void finished(bool ok, const pipeline::PipelineSnapshot& snapshot);

private:
    CompilerRunnerBackend& backend_;
    pipeline::LexerPipeline pipeline_;
    std::atomic<bool> stop_{false};
};

} // namespace compiler::gui
