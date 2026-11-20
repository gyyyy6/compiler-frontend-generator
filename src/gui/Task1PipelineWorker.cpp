#include "Task1PipelineWorker.h"

#include <QString>

#include "CompilerRunnerBackend.h"

Q_DECLARE_METATYPE(compiler::pipeline::PipelineSnapshot)

namespace compiler::gui {
namespace {

QString stageName(compiler::pipeline::Stage stage)
{
    switch (stage) {
    case compiler::pipeline::Stage::Rules:    return QObject::tr("解析规则");
    case compiler::pipeline::Stage::Nfa:      return QObject::tr("构建 NFA");
    case compiler::pipeline::Stage::Dfa:      return QObject::tr("构建 DFA");
    case compiler::pipeline::Stage::Minimize: return QObject::tr("最小化 DFA");
    case compiler::pipeline::Stage::Codegen:  return QObject::tr("生成扫描器源码");
    case compiler::pipeline::Stage::Compile:  return QObject::tr("编译扫描器");
    case compiler::pipeline::Stage::Run:      return QObject::tr("运行扫描器");
    default:                                  return QObject::tr("未知阶段");
    }
}

} // namespace

Task1PipelineWorker::Task1PipelineWorker(CompilerRunnerBackend& backend, QObject* parent)
    : QObject(parent)
    , backend_(backend)
    , pipeline_(backend_)
{
    qRegisterMetaType<pipeline::PipelineSnapshot>();
    pipeline_.setStageCallback([this](pipeline::Stage s) {
        emit stageStarted(stageName(s));
    });
    pipeline_.setStopCallback([this]() { return stop_.load(); });
}

Task1PipelineWorker::~Task1PipelineWorker() = default;

void Task1PipelineWorker::requestStop()
{
    stop_.store(true);
}

bool Task1PipelineWorker::stopRequested() const
{
    return stop_.load();
}

void Task1PipelineWorker::runTask(int stage, const QString& rulesText,
                                  const QString& rulesPath,
                                  const QString& sourcePath,
                                  const QString& outputDir)
{
    if (stop_.load()) {
        emit finished(false, pipeline_.snapshot());
        return;
    }

    pipeline_.setRulesText(rulesText.toStdString());
    pipeline_.setRulesPath(rulesPath.toStdString());
    pipeline_.setOutputDir(outputDir.toStdString());

    if (!sourcePath.isEmpty()) {
        if (!pipeline_.setSourcePath(sourcePath.toStdString())) {
            emit logMessage(tr("[Error] [input] 读取源程序文件失败：%1").arg(sourcePath));
        }
    } else {
        pipeline_.setSourceText({});
    }

    const bool ok = pipeline_.runThrough(static_cast<pipeline::Stage>(stage));
    emit finished(ok, pipeline_.snapshot());
}

} // namespace compiler::gui
