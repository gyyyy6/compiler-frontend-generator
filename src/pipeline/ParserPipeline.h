#pragma once

// 任务二语法分析流水线编排（T2.10，方案见 docs/PROGRESS.md「T2.10」）
// 纯 C++17 + STL，不依赖 Qt。
//
// 职责：把 T2.1~T2.9 串成一条可断点续跑的流水线——
//   LexRead(读 sample.lex 头部 registry + token 流) → BnfParse(.bnf → Grammar)
//   → Simplify(化简) → FirstFollow → Lr0(LR(0) 项目集族) → Lalr1(LALR(1))
//   → ParseTable(ACTION/GOTO) → Drive(LR 驱动 + 语法树) → Ir(四元式中间代码)
//
// LexRead 前置（与任务书阶段顺序的一处必要偏离）：
//   BnfParser 需要 TokenCodeRegistry 做「终结符→tokenCode」映射（主 A：读
//   sample.lex 头部 fromHeader 重建），故 sample.lex 读取放在 BnfParse 之前；
//   其余算法严格按题给顺序执行。备 B（重解析 .rules）仅供测试，pipeline 不引入。
//
// 降级路径（无 sample.lex）：
//   - runThrough(ParseTable) 及更早：LexRead 跳过，BnfParser 以 registry=nullptr
//     运行（终结符 tokenCode=-1，T2.1 已支持），记 [Info] 诊断，流水线继续；
//   - runThrough(Drive/Ir)：LexRead 为硬前置，失败即停（stage="sample-lex-read"）。
//
// 失效规则（输入变更后的产物失效范围）：
//   - setBnfText：BnfParse..Ir 失效（LexRead 保留）；
//   - setSampleLexText：全部失效（registry 可能变化，BnfParse 需以新 registry 重跑）。
//
// 失败语义：runThrough 任一步失败立即返回 false，诊断（自带 stage/message/行号）
// 累积于 diagnostics()；GUI 在日志 Dock 显示 stage 与 message（与 T1.9 一致）。

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Diagnostic.h"
#include "FirstFollow.h"
#include "Grammar.h"
#include "GrammarSimplifier.h"
#include "IrGenerator.h"
#include "Lalr1Builder.h"
#include "Lr0Automaton.h"
#include "ParseTable.h"
#include "ParseTree.h"
#include "Token.h"
#include "TokenCodeRegistry.h"

namespace compiler::pipeline {

// 流水线阶段（升序；runThrough(target) 自动补齐缺失的上游阶段）
// None = 无任何产物有效（初始态 / sample.lex 变更后回退到该态）
// 注意：与 LexerPipeline::Stage 同名冲突规避，故命名为 ParseStage。
enum class ParseStage : int {
    None = -1,
    LexRead = 0,   // SampleLexReader：头部 registry + token 流（主 A 前置）
    BnfParse,      // BnfParser（registry 可用时映射终结符→tokenCode）
    Simplify,      // GrammarSimplifier
    FirstFollow,   // FirstFollow
    Lr0,           // Lr0Automaton
    Lalr1,         // Lalr1Builder
    ParseTable,    // ParseTableBuilder
    Drive,         // LrDriver + ParseTree
    Ir,            // IrGenerator
};

// 产物快照：GUI/测试一次性读取（值语义跨线程；ParseTree 含 unique_ptr 故用 shared_ptr）
struct ParserPipelineSnapshot {
    bool ok = false;                    // 最近一次 runThrough 是否整体成功
    ParseStage validThrough = ParseStage::None;  // 已推进到的最远阶段

    Grammar        grammar;             // BnfParse 原始文法
    Grammar        simplifiedGrammar;   // Simplify 化简后（下游全部消费它）
    grammar::SimplifyLog simplifyLog;   // 化简日志（删除符号/产生式 + 摘要）
    grammar::FirstFollowSets firstFollow;  // First/Follow/Nullable（按化简后符号 id 索引）
    parser::Lr0AutomatonData lr0;       // LR(0) 项目集族
    parser::Lalr1Data      lalr1;       // LALR(1)（LR(1) + 同心合并）
    parser::ParseTableData parseTable;  // ACTION/GOTO + 冲突报告

    TokenCodeRegistry registry;         // LexRead 从 sample.lex 头部重建（无 sample.lex 时仅保留项）
    std::vector<Token> tokens;          // sample.lex 数据行 token 流（无 EOF；LrDriver 自补）
    std::shared_ptr<const ParseTree> parseTree;  // 语法树（Drive 成功后才非空）
    ir::IrProgram ir;                   // 四元式中间代码

    std::string languageName;           // sample.lex #language → bnf 主干 → "lang"
    std::vector<Diagnostic> diagnostics;  // 本次 runThrough 全部阶段累积
};

// 任务二语法分析流水线（零 Qt；进程内，无需 CompileRunBackend——决策点 4）
class ParserPipeline {
public:
    ParserPipeline() = default;
    ~ParserPipeline() = default;

    // ---- 输入 ----
    // BNF 文本变更：BnfParse..Ir 失效（LexRead 保留）
    void setBnfText(const std::string& text);
    // BNF 文件路径（仅用于语言名派生与日志展示；不读取文件）
    void setBnfPath(const std::string& path);
    // sample.lex 文本变更：全部失效（registry 可能变化，BnfParse 需重跑）
    void setSampleLexText(const std::string& text);
    // sample.lex 文件路径（仅展示）
    void setSampleLexPath(const std::string& path);
    // 读取 sample.lex 文件（UTF-8 去 BOM）后 setSampleLexText；文件不可读返回 false
    bool setSampleLexFile(const std::string& path);

    // 阶段边界中止回调：返回 true 请求取消（GUI 关闭窗口时置位，避免阻塞退出）
    void setStopCallback(std::function<bool()> cb);
    // 阶段开始回调：runThrough 每进入一个阶段前调用（GUI 进度提示）
    void setStageCallback(std::function<void(ParseStage)> cb);

    // 推进到 target 阶段（含）；任一步失败即返回 false，诊断见 diagnostics()
    bool runThrough(ParseStage target);

    // T3.4：整段复位（清空 LexRead..Ir 全部产物与诊断，currentStage_ 回到 None）。
    // 供 JointPipeline 在词法输入（rules/source）变更时调用，避免把上一轮的语法树/
    // 分析表当成新输入的结果；等价于 invalidateFrom(LexRead) + 清诊断。
    void reset();

    // ---- 查询 ----
    ParseStage currentStage() const { return currentStage_; }
    const std::vector<Diagnostic>& diagnostics() const { return diags_; }
    ParserPipelineSnapshot snapshot() const;

private:
    // 单阶段实现（返回 false 表示该阶段失败，runThrough 停止）
    bool runStageLexRead(int targetIdx);  // 需知最终 target 以判定降级 vs 硬失败
    bool runStageBnfParse();
    bool runStageSimplify();
    bool runStageFirstFollow();
    bool runStageLr0();
    bool runStageLalr1();
    bool runStageParseTable();
    bool runStageDrive();
    bool runStageIr();

    // 清空 fromIdx 及其之后全部阶段产物，currentStage_ 回退
    void invalidateFrom(int fromIdx);
    void addDiag(Severity sev, const std::string& stage, const std::string& msg, int line = -1);
    bool stopRequested() const;
    std::string languageName() const;
    static std::string stripBom(const std::string& s);
    static bool readFile(const std::string& path, std::string& out, std::string& err);
    static std::string fileStem(const std::string& path);

    // 输入
    std::string bnfText_;
    std::string bnfPath_;
    std::string sampleLexText_;
    std::string sampleLexPath_;
    std::string sampleLexLang_;          // sample.lex #language（语言名派生优先源）

    // 阶段产物
    Grammar        grammar_;
    Grammar        simplifiedGrammar_;
    grammar::SimplifyLog simplifyLog_;
    grammar::FirstFollowSets firstFollow_;
    parser::Lr0AutomatonData lr0_;
    parser::Lalr1Data      lalr1_;
    parser::ParseTableData parseTable_;
    TokenCodeRegistry registry_;
    std::vector<Token> tokens_;
    bool lexDataOk_ = false;             // LexRead 成功（有有效 token 流）
    std::shared_ptr<const ParseTree> parseTree_;
    ir::IrProgram ir_;

    // 运行态
    ParseStage currentStage_ = ParseStage::None;
    bool lastRunOk_ = false;
    std::vector<Diagnostic> diags_;
    std::function<bool()> stopCb_;
    std::function<void(ParseStage)> stageCb_;
};

// 语法树 → 缩进文本（CLI --parse / 单测 / 诊断复用；内部节点带 [prod N]）
std::string formatParseTree(const ParseTree& tree);

// 四元式 → 每行 (op, arg1, arg2, result)
std::string formatIrProgram(const ir::IrProgram& ir);

} // namespace compiler::pipeline
