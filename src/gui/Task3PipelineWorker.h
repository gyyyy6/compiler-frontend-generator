#pragma once

// 任务三联合测试工作线程（T3.5，方案见 docs/PROGRESS.md「T3.5 实施方案」）
//
// 职责一：在独立 QThread 中执行 JointPipeline（真实生成扫描器 → LALR(1)
//   语法树 → 四元式 IR），结果以 JointSnapshot 值语义跨线程回传 GUI；
//   关闭页面时先 requestStop() 再退出线程（与 T1.9/T2.10 同一模式）。
//
// 职责二：任务三「覆盖矩阵 / 阶段时间线 / Markdown 测试报告」的纯函数。
//   放在此处的原因：JointPipeline（T3.4，已提交）只暴露阶段名与
//   formatParseTree()/formatIrProgram()，本身不含报告渲染；而 GUI「导出报告」
//   与 `--minic` CLI 必须输出同一份报告，故由本模块统一渲染，内部直接调用
//   pipeline 的格式化函数（不重复实现树/IR 文本）。
//
// 线程模型：
//   Task3Page 持有 QThread + Task3PipelineWorker；worker 内持 CompilerRunnerBackend
//   与 JointPipeline（同一实例贯穿多次单步，故「仅词法」→「仅语法」可续跑）。
//
// 生命周期（Task3Page 析构）：
//   worker_->requestStop();  thread_->quit();  thread_->wait();
//   delete worker_;          delete thread_;

#include <QObject>

#include <atomic>
#include <string>
#include <vector>

#include "CompilerRunnerBackend.h"
#include "JointPipeline.h"

class QString;

namespace compiler::gui {

// 运行模式（runTask 的 mode 参数；与 Q_ARG 传递兼容用 int）
enum class JointRunMode : int {
    Joint = 0,    // 一键联合测试：runThrough(JointStage)（强制三段，含严格校验）
    Lexer = 1,    // 仅词法测试：runLexerThrough(Stage)（target==Run 时执行严格校验）
    Parser = 2,   // 仅语法测试：runParserThrough(ParseStage)（前置 strictLexOk）
    Preset = 3,   // 载入预置 sample.lex（独立发布环境缺 g++ 时的演示通道）
};

// 覆盖项：关键字 / 符号（终结符）或一条产生式
struct CoverageItem {
    std::string label;      // "KW_IF"、"SYM_SEMI"、"12: expr -> expr + term"
    bool covered = false;   // 是否在本次 token 流 / 语法树中出现
    bool epsilon = false;   // ε 产生式：结构可达但归约不产生树节点，不计入覆盖率分母
};

// 覆盖矩阵（Tab1 与报告共用；全部来自联合快照，不重跑流水线）
struct JointCoverage {
    std::vector<CoverageItem> keywords;      // KW_* 终结符
    std::vector<CoverageItem> symbols;       // SYM_* 终结符
    std::vector<CoverageItem> productions;   // 化简后文法全部产生式
};

// 从快照计算覆盖矩阵。parse 阶段未推进时各分组为空。
JointCoverage computeJointCoverage(const pipeline::JointSnapshot& snapshot);

// 覆盖率文本："34/35（97.1%）"；全为 ε 或空集时返回 "—"
std::string coverageSummary(const std::vector<CoverageItem>& items);

// 快照中的 token 流：优先 parse.tokens（LexRead 产物）；仅词法运行、尚未到语法
// 阶段时，用 SampleLexReader 解析 lex.runLexText（仅供展示/统计，不参与分析）。
std::vector<Token> jointTokens(const pipeline::JointSnapshot& snapshot);

// 16 阶段时间线（Markdown 表格）：Rules→Nfa→Dfa→Minimize→Codegen→Compile→Run
//   →LexRead→BnfParse→Simplify→FirstFollow→Lr0→Lalr1→ParseTable→Drive→Ir
// 状态由快照的 validThrough 与首条 Error 诊断的阶段共同推导。
std::string formatJointTimeline(const pipeline::JointSnapshot& snapshot);

// 报告上下文：快照之外的展示信息（路径、输出目录）。GUI 传入用户打开的文件，
// CLI 传命令行参数；sourcePath 为编辑器内容时请注明（如 "（编辑器内容）"）。
struct JointReportContext {
    std::string rulesPath;
    std::string bnfPath;
    std::string sourcePath;
    std::string outputDir;
};

// 完整 Markdown 测试报告：结论 / 阶段时间线 / 词法结果 / 语法结果（冲突+树+IR）/
// 覆盖矩阵 / 诊断汇总。直接复用 pipeline::formatParseTree / formatIrProgram。
std::string formatJointReportMarkdown(const pipeline::JointSnapshot& snapshot,
                                      const JointReportContext& context = {});

class Task3PipelineWorker : public QObject {
    Q_OBJECT

public:
    explicit Task3PipelineWorker(QObject* parent = nullptr);
    ~Task3PipelineWorker() override;

    // 线程安全：请求中止当前/后续阶段（阶段边界生效）
    void requestStop();
    bool stopRequested() const;

public slots:
    // 在 worker 线程执行：应用输入 → 按 mode 推进 → 发 finished。
    //   mode  : JointRunMode 的 int 值
    //   stage : 对应 JointStage / Stage / ParseStage 的 int 值
    // 源程序统一以 sourceText 传入（由 LexerPipeline 落盘
    // <outputDir>/<lang>/_input_src.txt）。不使用 setSourcePath：该接口在
    // 「先读文件、后改文本」时会保留旧路径，导致扫描旧文件。
    void runTask(int mode, int stage,
                 const QString& rulesText, const QString& rulesPath,
                 const QString& bnfText, const QString& bnfPath,
                 const QString& sourceText,
                 const QString& outputDir);

signals:
    // group："联合" / "词法" / "语法"
    void stageStarted(const QString& group, const QString& name);
    void logMessage(const QString& message);
    void finished(bool ok, const compiler::pipeline::JointSnapshot& snapshot,
                  qint64 elapsedMs);

private:
    // 输入应用（内容未变时子流水线内部短路，不会误失效）
    void applyInputs(const QString& rulesText, const QString& rulesPath,
                     const QString& bnfText, const QString& bnfPath,
                     const QString& sourceText,
                     const QString& outputDir);

    CompilerRunnerBackend backend_;              // 必须在 joint_ 之前构造
    pipeline::JointPipeline joint_;

    std::atomic<bool> stop_{false};
};

} // namespace compiler::gui
