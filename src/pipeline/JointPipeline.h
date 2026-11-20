#pragma once

// 任务三联合流水线编排（T3.4，方案见 docs/PROGRESS.md「T3.4」/ ARCHITECTURE.md §9.1）
// 纯 C++17 + STL，不依赖 Qt。
//
// 职责：严格串联任务一与任务二两条子流水线——
//   LexRun：LexerPipeline::runThrough(Run)   Rules→Nfa→Dfa→Minimize→Codegen→Compile→Run
//   Parse ：ParserPipeline::runThrough(Drive) LexRead→BnfParse→Simplify→FirstFollow
//                                             →Lr0→Lalr1→ParseTable→Drive
//   Ir    ：ParserPipeline::runThrough(Ir)    IrGenerator（选做转必做）
//
// 绝对强制校验（T3.1 决策点 4）：
//   词法阶段推进到 Run 之后，必须复核 PipelineSnapshot::runProducedLex。
//   若为 false 且已到 Compile/Run（或存在预览文本）——即生成扫描器的编译/运行
//   失败后回退到进程内预览——记 Severity::Error + stage="joint-strict" 并立即
//   终止；若上游阶段就失败（根本没有 sample.lex 可产）记 stage="joint-lex"。
//   两种情况都不允许 ParserPipeline 被调用：预览文本仅作诊断，绝不喂给语法分析。
//   语法阶段前置不满足时记 stage="joint-parse"。
//
// 推进粒度：
//   - runThrough(JointStage)        一键联合（GUI「一键联合测试」/ CLI --minic）
//   - runLexerThrough(Stage)        词法单步（GUI「仅词法测试」/ 单步按钮）
//   - runParserThrough(ParseStage)  语法单步（需严格词法已通过）
//
// 失效规则：
//   - setRulesText / setSourceText / setSourcePath / setOutputDir：
//     LexRun 起（含 Parse/Ir）失效，并复位 ParserPipeline（防止旧语法树被当成新结果）；
//   - setBnfText：仅 Parse/Ir 失效，词法产物保留。

#include <functional>
#include <string>
#include <vector>

#include "Diagnostic.h"
#include "LexerPipeline.h"
#include "ParserPipeline.h"

namespace compiler::pipeline {

// 联合阶段（升序；runThrough(target) 自动补齐缺失的上游阶段）
enum class JointStage : int {
    None = -1,
    LexRun = 0,   // 任务一：真实生成扫描器运行并产出 sample.lex（强制 runProducedLex）
    Parse,        // 任务二：LALR(1) + LR 驱动 + 语法树
    Ir,           // 任务二扩展：四元式中间代码
};

// 联合快照：词法 + 语法全部产物 + 合并诊断（值语义，可跨线程拷贝回 GUI）
struct JointSnapshot {
    bool ok = false;                       // 最近一次 runThrough 是否整体成功
    JointStage validThrough = JointStage::None;  // 联合层已推进到的最远阶段
    Stage      lexStage = Stage::None;     // 词法子流水线当前阶段
    ParseStage parseStage = ParseStage::None;    // 语法子流水线当前阶段

    bool runProducedLex = false;           // 词法产物是否来自生成扫描器真实运行
    bool strictLexOk = false;              // 联合层严格校验是否通过（Parse 的前置）

    // 预置产物模式：本机未调用生成扫描器，直接载入随包分发的 sample.lex
    // （独立发布环境缺 g++ 时的演示通道；与"进程内预览"是两回事，
    //   预览永远不落盘、也永远不进语法分析，见 REPORT §2.4.6）
    bool presetLex = false;                // 本次链路的 sample.lex 是否来自预置文件
    std::string presetLexPath;             // 预置 sample.lex 的实际路径

    PipelineSnapshot lex;                  // 任务一全部产物（NFA/DFA/最小DFA/源码/sample.lex 文本）
    ParserPipelineSnapshot parse;          // 任务二全部产物（文法/集合/自动机/表/语法树/IR）

    std::string sampleLexPath;             // 生成扫描器产出的 sample.lex 路径
    std::string sourcePath;                // 源程序路径（无路径时为临时落盘输入）
    std::string languageName;              // 语言名（取自 .rules @name）

    // 合并诊断：词法阶段 → 语法阶段 → 联合层结论（顺序固定，便于报告时间线）
    std::vector<Diagnostic> diagnostics;
};

// 任务三联合流水线（零 Qt）
class JointPipeline {
public:
    // 编译/运行后端由外部注入（src/gui 的 CompilerRunnerBackend；测试可注入 fake）
    explicit JointPipeline(CompileRunBackend& backend);
    ~JointPipeline() = default;

    // ---- 输入 ----
    void setRulesText(const std::string& text);
    void setRulesPath(const std::string& path);          // 仅用于语言名派生与展示
    void setSourceText(const std::string& text);
    bool setSourcePath(const std::string& path);          // 读取源文件（UTF-8 去 BOM）
    void setOutputDir(const std::string& dir);            // 默认 "build/generated"
    void setBnfText(const std::string& text);
    void setBnfPath(const std::string& path);             // 仅用于展示

    // 便捷入口（CLI / GUI 共用）：一次性读取 .rules / .bnf / 源程序
    bool loadInputs(const std::string& rulesPath,
                    const std::string& bnfPath,
                    const std::string& sourcePath);

    // ---- 回调（转发给两条子流水线）----
    void setStopCallback(std::function<bool()> cb);
    void setStageCallback(std::function<void(JointStage)> cb);
    void setLexStageCallback(std::function<void(Stage)> cb);
    void setParseStageCallback(std::function<void(ParseStage)> cb);

    // ---- 推进 ----
    // 推进到 target 联合阶段（含）；任一步失败即返回 false，诊断见 diagnostics()
    bool runThrough(JointStage target);    // 词法单步：推进到给定词法子阶段（target==Run 时执行严格校验）
    bool runLexerThrough(Stage target);
    // 语法单步：推进到给定语法子阶段（前置：strictLexOk()）
    bool runParserThrough(ParseStage target);

    // ---- 预置产物通道（独立发布环境专用）----
    // 载入指定的 sample.lex 作为词法产物：跳过 Codegen/Compile/Run 三段，
    // 直接进入 LexRun 并把文本注入语法流水线。文本必须是任务一真实运行产出
    // 的格式（头部登记表 + 数据区），否则语法阶段会在 LexRead 报错。
    // 语义边界：这条通道只解决"本机没有 g++"，绝不复用进程内预览文本。
    bool loadPresetLex(const std::string& lexPath);
    // 按 <outputDir>/<语言名>/sample.lex 自动定位；目录名与语言名大小写
    // 不一致时，退化为扫描 <outputDir>/*/sample.lex 取第一个可读文件。
    bool loadPresetLexFromOutputDir();
    bool presetLexUsed() const { return presetLexUsed_; }

    // ---- 查询 ----
    JointStage currentStage() const { return currentStage_; }
    Stage      lexStage() const { return lex_.currentStage(); }
    ParseStage parseStage() const { return parser_.currentStage(); }
    bool strictLexOk() const { return strictLexOk_; }
    const std::vector<Diagnostic>& diagnostics() const { return diags_; }
    JointSnapshot snapshot() const;

private:
    // 单阶段实现
    bool runStageLexRun();
    bool runStageParse();
    bool runStageIr();

    // 严格校验：词法必须真实产出 sample.lex
    //   - runProducedLex == false 且已到 Compile/Run 或存在预览 → joint-strict（预览回退，绝对拒绝）
    //   - runProducedLex == false 且上游阶段就失败 → joint-lex（无 sample.lex 可产）
    bool verifyLexProduced(const PipelineSnapshot& lexSnap);

    // 失效：currentStage_ 回退到 from 的前一阶段；from==LexRun 时同步复位语法流水线
    void invalidateFrom(JointStage from);
    // 把 runLexText 与 sampleLexPath 注入语法流水线
    void feedParser(const std::string& runLexText);
    // 收集子流水线本次 runThrough 的诊断（子流水线每次调用都会清空自身诊断，
    // 故联合层按阶段累积，保证快照里能看到词法+语法全过程的诊断）
    void collectLexDiagnostics();
    void collectParseDiagnostics();
    // 重算合并诊断（词法 → 语法 → 联合）
    void rebuildDiagnostics();
    void addDiag(Severity sev, const std::string& stage, const std::string& msg, int line = -1);
    bool stopRequested() const;
    std::string sampleLexPathFor(const std::string& languageName) const;
    static bool readFile(const std::string& path, std::string& out, std::string& err);

    LexerPipeline  lex_;
    ParserPipeline parser_;

    std::string outputDir_ = "build/generated";
    std::string sourcePath_;
    std::string bnfTextCache_;   // 复位语法流水线后重放 BNF 输入用
    std::string bnfPathCache_;
    std::string rulesTextCache_;  // 判断输入是否真变（预置产物据此作废）
    std::string sourceTextCache_;

    bool strictLexOk_ = false;
    std::string sampleLexPath_;  // <outputDir>/<语言名>/sample.lex（真实产出后非空）

    // 预置产物状态：显式载入后，LexRun 段短路，语法阶段基于该文本继续
    bool presetLexUsed_ = false;
    std::string presetLexText_;
    std::string presetLexPath_;

    JointStage currentStage_ = JointStage::None;
    bool lastRunOk_ = false;

    std::vector<Diagnostic> jointDiags_;    // 联合层自有诊断
    std::vector<Diagnostic> lexDiags_;      // 词法子流水线各阶段累积诊断
    std::vector<Diagnostic> parseDiags_;    // 语法子流水线各阶段累积诊断
    std::vector<Diagnostic> diags_;         // 合并视图（snapshot()/diagnostics() 返回它）

    std::function<bool()> stopCb_;
    std::function<void(JointStage)> stageCb_;
};

// 阶段名（CLI 报告 / 日志 / 诊断复用）
const char* jointStageName(JointStage s);
const char* lexStageName(Stage s);
const char* parseStageName(ParseStage s);

} // namespace compiler::pipeline
