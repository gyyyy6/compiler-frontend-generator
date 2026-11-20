#pragma once

// 任务二流水线工作线程（T2.10）
// 在独立 QThread 中执行 ParserPipeline（任务二全算法，进程内、无外部编译运行），
// 结果经快照信号回传 GUI 线程；关闭窗口时先 requestStop() 再退出线程。
//
// 生命周期（Task2Page 析构）：
//   worker_->requestStop();  thread_->quit();  thread_->wait();
//   delete worker_;          delete thread_;

#include <QObject>

#include <atomic>

#include "ParserPipeline.h"

class QString;

namespace compiler::gui {

// 流水线产物快照（跨线程经排队连接传递；qRegisterMetaType 见 .cpp）
class Task2PipelineWorker : public QObject {
    Q_OBJECT

public:
    explicit Task2PipelineWorker(QObject* parent = nullptr);
    ~Task2PipelineWorker() override;

    // 线程安全：请求中止当前/后续阶段（阶段边界生效）
    void requestStop();
    bool stopRequested() const;

public slots:
    // 在 worker 线程执行：设置输入 → runThrough(stage) → 发 finished。
    // stage 为 pipeline::ParseStage 的 int 值。
    void runTask(int stage, const QString& bnfText, const QString& bnfPath,
                 const QString& sampleLexText, const QString& sampleLexPath);

signals:
    void stageStarted(const QString& name);
    void logMessage(const QString& message);
    void finished(bool ok, const pipeline::ParserPipelineSnapshot& snapshot);

private:
    pipeline::ParserPipeline pipeline_;
    std::atomic<bool> stop_{false};
};

} // namespace compiler::gui
