#pragma once

// 任务一流水线编排（T1.9，方案见 docs/PROGRESS.md「T1.9」）
// 纯 C++17 + STL，不依赖 Qt。
//
// 职责：把 T1.6~T1.8 串成一条可断点续跑的流水线——
//   Rules(解析 .rules) → Nfa → Dfa → Minimize → Codegen(生成扫描器源码)
//   → Compile(写盘 + 调用系统编译器) → Run(运行生成扫描器产出 sample.lex)
//
// 与 LexerCompilerRunner 的关系（依赖反转）：
//   编译/运行通过 CompileRunBackend 抽象注入；src/gui 提供 Qt 适配器
//   （CompilerRunnerBackend，包 LexerCompilerRunner），测试可注入 fake backend，
//   pipeline 本身保持零 Qt。长期考虑：T2 也走 pipeline 时，CompileRunBackend
//   可抽到 src/core/（T1.9 暂留本模块，不阻塞）。
//
// 失效规则（输入变更后的产物失效范围）：
//   - setRulesText：NFA/DFA/最小DFA/生成源码/sample.lex 全部失效；
//   - setSourcePath / setSourceText：仅 sample.lex（含进程内预览）失效，
//     上游 NFA/DFA/最小DFA/生成源码/已编译可执行文件全部保留。
//
// sample.lex 两种来源（诚实标注由 GUI 负责，见 PipelineSnapshot）：
//   - 真实运行：Run 阶段成功，runLexText 为生成扫描器产出（runProducedLex=true）；
//   - 进程内预览：编译或运行失败时回退，用 Scanner+SampleLexWriter 渲染
//     （runProducedLex=false），GUI 必须标注"[进程内预览，非真实运行结果]"。
//
// 失败语义：runThrough 任一步失败立即返回 false，诊断（自带 stage/message/行号）
// 累积于 diagnostics()；GUI 在日志 Dock 显示 stage 与 message。

#include <functional>
#include <string>
#include <vector>

#include "DFA.h"
#include "Diagnostic.h"
#include "LexerSpec.h"
#include "NFA.h"

namespace compiler::pipeline {

// 编译/运行后端抽象（零 Qt；src/gui 的 CompilerRunnerBackend 包 LexerCompilerRunner 实现）
struct CompileRunBackend {
    virtual ~CompileRunBackend() = default;

    // 编译 sourceFile → outputExe；成功返回 true，失败填 errorOut（建议含 g++ stderr）
    virtual bool compile(const std::string& sourceFile,
                         const std::string& outputExe,
                         std::string& errorOut) = 0;

    // 运行 exe（参数 args）；stdOut/stdErr/exitCode 回传进程输出；退出码 0 返回 true
    virtual bool run(const std::string& exe,
                     const std::vector<std::string>& args,
                     std::string& stdOut,
                     std::string& stdErr,
                     int& exitCode) = 0;
};

// 流水线阶段（升序；runThrough(target) 自动补齐缺失的上游阶段）
// None = 无任何产物有效（初始态 / 规则文本变更后回退到该态）
enum class Stage : int {
    None = -1,
    Rules = 0,   // 解析 .rules → LexerSpec
    Nfa,         // LexerBuilder::buildNfa
    Dfa,         // regex::subsetConstruct
    Minimize,    // regex::minimizeDfa
    Codegen,     // ScannerSourceGenerator::generate → generated_lexer.cpp 源码
    Compile,     // 写盘 generated_lexer.cpp + backend.compile → <lang>_scanner(.exe)
    Run,         // backend.run(<exe> <source> <sample.lex>) + 回读
};

// 产物快照：GUI/测试一次性读取（NFA/DFA 体积小，直接拷贝跨线程）
struct PipelineSnapshot {
    bool  ok = false;              // 最近一次 runThrough 是否整体成功
    bool  runProducedLex = false;  // runLexText 是否来自生成扫描器真实运行
    Stage validThrough = Stage::None;  // 已推进到的最远阶段（GUI 据此刷新/清空子 Tab）
    Nfa   nfa;
    Dfa   dfa;
    Dfa   minimalDfa;
    std::string generatedSource;   // generated_lexer.cpp 全文
    std::string previewLexText;    // 进程内渲染（仅编译/运行失败回退时非空）
    std::string runLexText;        // 真实运行产出的 sample.lex 全文
    std::string languageName;      // 语言名（@name 或规则文件主干，缺省 "lang"）
    std::string exePath;           // 生成扫描器可执行文件路径（Compile 成功后非空）
    std::vector<Diagnostic> diagnostics;  // 本次 runThrough 全部阶段累积
};

// 任务一词法分析流水线（零 Qt）
class LexerPipeline {
public:
    explicit LexerPipeline(CompileRunBackend& backend);
    ~LexerPipeline() = default;

    // ---- 输入 ----
    // 规则文本变更：NFA/DFA/最小DFA/生成源码/sample.lex 全部失效
    void setRulesText(const std::string& text);
    // 规则文件路径（仅用于语言名派生与日志展示；不读取文件）
    void setRulesPath(const std::string& path);
    // 源程序文本变更：仅 sample.lex（含预览）失效，上游保留
    void setSourceText(const std::string& text);
    // 当前源程序文本（已去 BOM）：联合层据此判断"预置产物是否仍与源程序对应"
    std::string sourceText() const { return sourceText_; }
    // 读取源程序文件（UTF-8 去 BOM）后 setSourceText；文件不可读返回 false
    bool setSourcePath(const std::string& path);
    // 产物输出目录（默认 "build/generated"，实际目录为 <dir>/<语言名>/）
    void setOutputDir(const std::string& dir);

    // 阶段边界中止回调：返回 true 请求取消（GUI 关闭窗口时置位，避免阻塞退出）
    void setStopCallback(std::function<bool()> cb);
    // 阶段开始回调：runThrough 每进入一个阶段前调用（GUI 进度提示）
    void setStageCallback(std::function<void(Stage)> cb);

    // 推进到 target 阶段（含）；任一步失败即返回 false，诊断见 diagnostics()
    bool runThrough(Stage target);

    // ---- 查询 ----
    Stage currentStage() const { return currentStage_; }
    const std::vector<Diagnostic>& diagnostics() const { return diags_; }
    PipelineSnapshot snapshot() const;

private:
    // 单阶段实现（返回 false 表示该阶段失败，runThrough 停止）
    bool runStageRules();
    bool runStageNfa();
    bool runStageDfa();
    bool runStageMinimize();
    bool runStageCodegen();
    bool runStageCompile();
    bool runStageRun();

    // 清空 from 阶段及之后的全部产物，currentStage_ 回退到 from 前一个阶段
    void invalidateFrom(Stage from);
    // 编译/运行失败时回退：进程内 Scanner+SampleLexWriter 渲染 sample.lex（零 Qt）
    bool buildPreviewLex();
    bool stopRequested();
    void addDiag(Severity sev, const std::string& stage,
                 const std::string& msg, int line = -1);

    // 路径与文件工具
    std::string languageName() const;            // spec.name 或规则文件主干，缺省 "lang"
    std::string langDir() const;                 // <outputDir>/<语言名>/
    bool ensureLangDir();                        // 创建目录；失败加诊断并返回 false
    std::string ensureSourceFile();              // 无 sourcePath 时把 sourceText 落盘为临时输入
    bool writeFile(const std::string& path, const std::string& data, std::string& err);
    bool readFile(const std::string& path, std::string& out, std::string& err);
    static std::string trim(const std::string& s);

    CompileRunBackend& backend_;
    std::function<bool()> stopCb_;
    std::function<void(Stage)> stageCb_;

    // 输入
    std::string rulesText_;
    std::string rulesPath_;
    std::string sourceText_;
    std::string sourcePath_;
    std::string outputDir_ = "build/generated";

    // 各阶段产物（默认构造 = 空/未就绪）
    lexer::LexerSpec spec_;
    Nfa  nfa_;
    Dfa  dfa_;
    Dfa  minimalDfa_;
    std::string generatedSource_;
    std::string previewLexText_;
    std::string runLexText_;
    bool  runProducedLex_ = false;
    std::string exePath_;

    Stage currentStage_ = Stage::None;           // 最近成功推进到的阶段
    bool  lastRunOk_ = false;                    // 最近一次 runThrough 结果（快照 ok 字段）
    std::vector<Diagnostic> diags_;              // 本次 runThrough 累积
};

} // namespace compiler::pipeline
