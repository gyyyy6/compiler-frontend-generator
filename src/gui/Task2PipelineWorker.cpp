#include "Task2PipelineWorker.h"

#include <QString>

Q_DECLARE_METATYPE(compiler::pipeline::ParserPipelineSnapshot)

namespace compiler::gui {
namespace {

QString stageName(compiler::pipeline::ParseStage stage)
{
    switch (stage) {
    case compiler::pipeline::ParseStage::LexRead:    return QObject::tr("读取 sample.lex");
    case compiler::pipeline::ParseStage::BnfParse:   return QObject::tr("解析 BNF 文法");
    case compiler::pipeline::ParseStage::Simplify:   return QObject::tr("文法化简");
    case compiler::pipeline::ParseStage::FirstFollow: return QObject::tr("First/Follow");
    case compiler::pipeline::ParseStage::Lr0:        return QObject::tr("构建 LR(0)");
    case compiler::pipeline::ParseStage::Lalr1:      return QObject::tr("构建 LALR(1)");
    case compiler::pipeline::ParseStage::ParseTable: return QObject::tr("生成分析表");
    case compiler::pipeline::ParseStage::Drive:      return QObject::tr("LR 驱动/语法树");
    case compiler::pipeline::ParseStage::Ir:         return QObject::tr("生成中间代码");
    default:                                         return QObject::tr("未知阶段");
    }
}

} // namespace

Task2PipelineWorker::Task2PipelineWorker(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<pipeline::ParserPipelineSnapshot>();
    pipeline_.setStageCallback([this](pipeline::ParseStage s) {
        emit stageStarted(stageName(s));
    });
    pipeline_.setStopCallback([this]() { return stop_.load(); });
}

Task2PipelineWorker::~Task2PipelineWorker() = default;

void Task2PipelineWorker::requestStop()
{
    stop_.store(true);
}

bool Task2PipelineWorker::stopRequested() const
{
    return stop_.load();
}

void Task2PipelineWorker::runTask(int stage, const QString& bnfText,
                                  const QString& bnfPath,
                                  const QString& sampleLexText,
                                  const QString& sampleLexPath)
{
    if (stop_.load()) {
        emit finished(false, pipeline_.snapshot());
        return;
    }

    pipeline_.setBnfText(bnfText.toStdString());
    pipeline_.setBnfPath(bnfPath.toStdString());
    pipeline_.setSampleLexText(sampleLexText.toStdString());
    pipeline_.setSampleLexPath(sampleLexPath.toStdString());

    const bool ok = pipeline_.runThrough(static_cast<pipeline::ParseStage>(stage));
    emit finished(ok, pipeline_.snapshot());
}

} // namespace compiler::gui
