// T3.4 单测：JointPipeline（任务三零 Qt 联合流水线）。
// 零 Qt：直接链 compiler_pipeline（+ lexer/grammar/parser/ir，经传递），fake CompileRunBackend。
// 编译：g++ -std=c++17 -O2 -Wall -Wextra（经 CTest / 手动）
// 运行：joint_pipeline_test <languages 根目录>（缺省 ./test/languages），退出码 0 = 通过
//
// 覆盖（docs/PROGRESS.md「T3.4」+ ARCHITECTURE.md §9.1）：
//   ① 真实端到端：fake backend 把进程内扫描得到的 sample.lex 文本落到 outPath（模拟
//      生成扫描器真实产出）→ LexRun→Parse→Ir 全通；锚点 Mini-C 128 态 / 1 个 S/R /
//      139 quads / 树根 program；sampleLexPath 指向 <outputDir>/MiniC/sample.lex；
//   ② 严格拒绝：编译失败 / 运行失败两条预览回退路径 → stage="joint-strict" Error，
//      Parse 完全未被调用（parse 快照为空），validThrough 停在 None；
//   ③ 前置拒绝：未完成严格词法就直接 runParserThrough → stage="joint-parse" Error；
//   ④ 失效：bnf 变更回退到 LexRun（词法产物保留）；source 变更回退到 None（语法全清）；
//   ⑤ 单步推进：runLexerThrough / runParserThrough 逐阶段推进，lexStage/parseStage 对齐；
//   ⑥ 诊断合并：快照同时含词法（rules-parse/nfa-build/dfa-build/compile/run）与语法
//      （bnf-parse/lr0-build/parse-table/parse/ir）阶段名；
//   ⑦ loadInputs 便捷入口；停表回调（requestStop）在阶段边界生效。

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "JointPipeline.h"
#include "LexRulesParser.h"
#include "LexerBuilder.h"
#include "SampleLexWriter.h"
#include "Scanner.h"

using compiler::Diagnostic;
using compiler::Severity;
using compiler::pipeline::CompileRunBackend;
using compiler::pipeline::JointPipeline;
using compiler::pipeline::JointSnapshot;
using compiler::pipeline::JointStage;
using compiler::pipeline::ParseStage;
using compiler::pipeline::PipelineSnapshot;
using compiler::pipeline::Stage;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool cond, const char* what)
{
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::cerr << "[FAIL] " << what << "\n";
    }
}

std::string loadFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

// 进程内扫描 → sample.lex 文本（与生成扫描器输出逐字节一致，T1.8/T1.10 已核）
std::string makeSampleLex(const std::string& rulesPath, const std::string& sourcePath,
                          const std::string& langName)
{
    compiler::lexer::LexRulesParser parser;
    auto parsed = parser.parseFile(rulesPath);
    if (!parsed.ok) {
        return {};
    }
    compiler::lexer::LexerBuilder builder;
    auto dfaRes = builder.buildMinimalDfa(parsed.value);
    if (!dfaRes.ok) {
        return {};
    }
    compiler::lexer::Scanner scanner;
    if (!scanner.configure(parsed.value, dfaRes.value)) {
        return {};
    }
    const std::string source = loadFile(sourcePath);
    compiler::lexer::ScanResult scanRes = scanner.scan(source, sourcePath);
    return compiler::lexer::SampleLexWriter::render(
        scanRes.registry, langName, sourcePath, scanRes.tokens);
}

bool hasDiag(const JointSnapshot& snap, const std::string& stage, Severity sev)
{
    for (const Diagnostic& d : snap.diagnostics) {
        if (d.stage == stage && d.severity == sev) {
            return true;
        }
    }
    return false;
}

bool hasStage(const JointSnapshot& snap, const std::string& stage)
{
    for (const Diagnostic& d : snap.diagnostics) {
        if (d.stage == stage) {
            return true;
        }
    }
    return false;
}

// ---- 假后端：compile 恒成功（写占位 exe）；run 把预设 sample.lex 文本写到 outPath ----
class FakeBackend : public CompileRunBackend {
public:
    bool failCompile = false;
    bool failRun = false;
    std::string lexText;      // run 阶段写出的 sample.lex 全文

    int compileCalls = 0;
    int runCalls = 0;

    bool compile(const std::string& sourceFile, const std::string& outputExe,
                 std::string& errorOut) override
    {
        ++compileCalls;
        (void)sourceFile;
        if (failCompile) {
            errorOut = "fake: g++ compile failed";
            return false;
        }
        std::ofstream out(outputExe, std::ios::binary | std::ios::trunc);
        out << "fake-exe";
        return out.good();
    }

    bool run(const std::string& exe, const std::vector<std::string>& args,
             std::string& stdOut, std::string& stdErr, int& exitCode) override
    {
        ++runCalls;
        (void)exe;
        (void)stdOut;
        if (failRun) {
            stdErr = "fake: scanner crashed";
            exitCode = 1;
            return false;
        }
        const std::string outPath = args.at(1);
        std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
        out << lexText;
        exitCode = 0;
        return out.good();
    }
};

std::filesystem::path makeTempDir(const char* tag)
{
    const auto base = std::filesystem::temp_directory_path() /
        ("compiler_joint_pipeline_test_" + std::string(tag));
    std::filesystem::remove_all(base);
    std::filesystem::create_directories(base);
    return base;
}

struct Inputs {
    std::string rulesPath;
    std::string bnfPath;
    std::string srcPath;
};

Inputs minicInputs(const std::string& langRoot)
{
    Inputs in;
    in.rulesPath = langRoot + "/minic/minic.rules";
    in.bnfPath = langRoot + "/minic/minic.bnf";
    in.srcPath = langRoot + "/minic/samples/test.minic";
    return in;
}

// ---- ① 真实端到端（Mini-C）----
void testFullJointRun(const std::string& langRoot)
{
    const auto tmp = makeTempDir("full");
    const Inputs in = minicInputs(langRoot);

    FakeBackend backend;
    backend.lexText = makeSampleLex(in.rulesPath, in.srcPath, "MiniC");
    check(!backend.lexText.empty(), "① 生成测试用 sample.lex 文本");

    JointPipeline p(backend);
    p.setOutputDir((tmp / "gen").string());
    check(p.loadInputs(in.rulesPath, in.bnfPath, in.srcPath), "① loadInputs 成功");

    check(p.runThrough(JointStage::Ir), "① runThrough(Ir) 全链路成功");
    check(p.currentStage() == JointStage::Ir, "① currentStage == Ir");
    check(p.lexStage() == Stage::Run, "① lexStage == Run");
    check(p.parseStage() == ParseStage::Ir, "① parseStage == Ir");
    check(p.strictLexOk(), "① strictLexOk == true（真实产出）");

    const JointSnapshot s = p.snapshot();
    check(s.ok, "① snapshot.ok == true");
    check(s.validThrough == JointStage::Ir, "① validThrough == Ir");
    check(s.runProducedLex, "① runProducedLex == true");
    check(s.strictLexOk, "① snapshot.strictLexOk == true");

    // 词法产物
    check(!s.lex.nfa.states.empty(), "① NFA 非空");
    check(!s.lex.dfa.states.empty(), "① DFA 非空");
    check(!s.lex.minimalDfa.states.empty(), "① 最小DFA 非空");
    check(!s.lex.generatedSource.empty(), "① 生成扫描器源码非空");
    check(s.lex.runLexText == backend.lexText, "① runLexText == 后端写出的 sample.lex");
    check(s.lex.previewLexText.empty(), "① 无预览回退（previewLexText 为空）");

    // sample.lex 路径：<outputDir>/MiniC/sample.lex，且文件真实存在
    check(s.sampleLexPath.find("/MiniC/sample.lex") != std::string::npos,
          "① sampleLexPath 指向 <outputDir>/MiniC/sample.lex");
    check(std::filesystem::exists(std::filesystem::path(s.sampleLexPath)),
          "① sample.lex 文件存在");
    check(s.languageName == "MiniC", "① languageName == MiniC");
    check(!s.sourcePath.empty(), "① sourcePath 非空");

    // 语法产物（锚点：128 态 / 1 个 S/R / 139 quads / 树根 program）
    check(s.parse.parseTree != nullptr, "① 语法树非空");
    check(static_cast<int>(s.parse.lr0.states.size()) == 128, "① LR(0) 128 态");
    check(static_cast<int>(s.parse.lalr1.states.size()) == 128, "① LALR(1) 128 态");
    check(static_cast<int>(s.parse.parseTable.conflicts.size()) == 1, "① 1 个冲突（悬挂 else）");
    check(static_cast<int>(s.parse.ir.quads.size()) == 139, "① 139 quads");
    check(static_cast<int>(s.parse.tokens.size()) == 416, "① token 流 416 个（来自 sample.lex）");

    if (s.parse.parseTree && s.parse.parseTree->root) {
        check(s.parse.parseTree->root->symbolId == s.parse.simplifiedGrammar.startSymbol,
              "① 语法树根 == 化简后开始符号");
        const auto* rootSym = s.parse.simplifiedGrammar.symbol(s.parse.parseTree->root->symbolId);
        check(rootSym != nullptr && rootSym->name == "program", "① 语法树根为 program");
    } else {
        check(false, "① 语法树根可读");
    }

    // ⑥ 诊断合并：词法阶段 + 语法阶段 + 联合层顺序
    check(hasStage(s, "rules-parse"), "⑥ 含词法 rules-parse 诊断");
    check(hasStage(s, "dfa-build"), "⑥ 含词法 dfa-build 诊断");
    check(hasStage(s, "run"), "⑥ 含词法 run 诊断");
    check(hasStage(s, "bnf-parse"), "⑥ 含语法 bnf-parse 诊断");
    check(hasStage(s, "lr0-build"), "⑥ 含语法 lr0-build 诊断");
    check(hasStage(s, "parse-table"), "⑥ 含语法 parse-table 诊断");
    check(hasStage(s, "ir"), "⑥ 含语法 ir 诊断");
    check(!hasDiag(s, "joint-strict", Severity::Error), "⑥ 全链路无 joint-strict 错误");

    // 阶段名工具
    check(std::string(compiler::pipeline::jointStageName(JointStage::Ir)) == "Ir",
          "阶段名 jointStageName");
    check(std::string(compiler::pipeline::lexStageName(Stage::Codegen)) == "Codegen",
          "阶段名 lexStageName");
    check(std::string(compiler::pipeline::parseStageName(ParseStage::ParseTable)) == "ParseTable",
          "阶段名 parseStageName");

    std::filesystem::remove_all(tmp);
}

// ---- ② 严格拒绝：编译失败 / 运行失败 → 预览绝不进入语法分析 ----
void runStrictRejectCase(const std::string& langRoot, bool failCompile, const char* tag)
{
    const auto tmp = makeTempDir(tag);
    const Inputs in = minicInputs(langRoot);

    FakeBackend backend;
    backend.lexText = makeSampleLex(in.rulesPath, in.srcPath, "MiniC");
    backend.failCompile = failCompile;
    backend.failRun = !failCompile;

    JointPipeline p(backend);
    p.setOutputDir((tmp / "gen").string());
    check(p.loadInputs(in.rulesPath, in.bnfPath, in.srcPath), "② loadInputs 成功");

    const bool ok = p.runThrough(JointStage::Parse);
    check(!ok, "② 预览回退 → runThrough(Parse) 失败");
    check(p.strictLexOk() == false, "② strictLexOk 复位 false");
    check(p.parseStage() == ParseStage::None, "② 语法流水线未被调用（parseStage==None）");

    const JointSnapshot s = p.snapshot();
    check(!s.ok, "② snapshot.ok == false");
    check(!s.runProducedLex, "② runProducedLex == false");
    check(s.validThrough == JointStage::None, "② validThrough 停在 None");
    check(hasDiag(s, "joint-strict", Severity::Error), "② 存在 stage=joint-strict 的 Error");
    check(!s.lex.previewLexText.empty(), "② 预览仅作诊断存在（previewLexText 非空）");
    check(s.sampleLexPath.empty(), "② 未记录 sampleLexPath");

    // 语法侧完全为空：预览没有被喂给语法分析
    check(s.parse.tokens.empty(), "② parse.tokens 为空");
    check(s.parse.parseTree == nullptr, "② parse.parseTree 为空");
    check(s.parse.grammar.productions.empty(), "② parse.grammar 为空");
    check(s.parse.ir.quads.empty(), "② parse.ir 为空");

    // 严格拒绝后再次一键推进：仍在词法闸门失败（绝不跳过词法直接进语法）
    check(!p.runThrough(JointStage::Parse), "② 再次 runThrough(Parse) 仍失败于词法闸门");
    check(hasDiag(p.snapshot(), "joint-strict", Severity::Error),
          "② 再次推进仍报 joint-strict（不跳过词法闸门）");

    // 直接调用语法单步：前置不满足 → joint-parse
    check(!p.runParserThrough(ParseStage::Drive), "② runParserThrough 被前置拒绝");
    check(hasDiag(p.snapshot(), "joint-parse", Severity::Error),
          "② 语法单步诊断为 stage=joint-parse 前置错误");
    check(p.parseStage() == ParseStage::None, "② 前置拒绝后语法流水线仍未启动");

    std::filesystem::remove_all(tmp);
}

// ---- ③/⑤ 单步推进 + 前置拒绝 ----
void testSingleStep(const std::string& langRoot)
{
    const auto tmp = makeTempDir("step");
    const Inputs in = minicInputs(langRoot);

    FakeBackend backend;
    backend.lexText = makeSampleLex(in.rulesPath, in.srcPath, "MiniC");

    JointPipeline p(backend);
    p.setOutputDir((tmp / "gen").string());
    check(p.loadInputs(in.rulesPath, in.bnfPath, in.srcPath), "⑤ loadInputs 成功");

    // 未完成严格词法时，语法单步被拒绝
    check(!p.runParserThrough(ParseStage::BnfParse), "③ 严格词法未通过 → runParserThrough 失败");
    check(hasDiag(p.snapshot(), "joint-parse", Severity::Error),
          "③ 诊断为 stage=joint-parse");
    check(p.parseStage() == ParseStage::None, "③ 语法流水线未启动");

    // 词法逐阶段推进
    check(p.runLexerThrough(Stage::Rules), "⑤ 词法单步 Rules");
    check(p.lexStage() == Stage::Rules, "⑤ lexStage == Rules");
    check(p.currentStage() == JointStage::None, "⑤ 联合阶段仍为 None（未到 Run）");
    check(p.runLexerThrough(Stage::Nfa), "⑤ 词法单步 Nfa");
    check(p.runLexerThrough(Stage::Dfa), "⑤ 词法单步 Dfa");
    check(p.runLexerThrough(Stage::Minimize), "⑤ 词法单步 Minimize");
    check(p.lexStage() == Stage::Minimize, "⑤ lexStage == Minimize");
    check(!p.strictLexOk(), "⑤ 未到 Run 时 strictLexOk == false");

    check(p.runLexerThrough(Stage::Run), "⑤ 词法单步 Run（含严格校验）");
    check(p.lexStage() == Stage::Run, "⑤ lexStage == Run");
    check(p.strictLexOk(), "⑤ strictLexOk == true");
    check(p.currentStage() == JointStage::LexRun, "⑤ 联合阶段推进到 LexRun");
    check(!p.snapshot().sampleLexPath.empty(), "⑤ sampleLexPath 已记录");

    // 语法逐阶段推进
    check(p.runParserThrough(ParseStage::BnfParse), "⑤ 语法单步 BnfParse");
    check(p.parseStage() == ParseStage::BnfParse, "⑤ parseStage == BnfParse");
    check(p.currentStage() == JointStage::LexRun, "⑤ 未到 Drive 时联合阶段仍 LexRun");
    check(p.runParserThrough(ParseStage::FirstFollow), "⑤ 语法单步 FirstFollow");
    check(p.runParserThrough(ParseStage::Lr0), "⑤ 语法单步 Lr0");
    check(p.runParserThrough(ParseStage::ParseTable), "⑤ 语法单步 ParseTable");
    check(p.parseStage() == ParseStage::ParseTable, "⑤ parseStage == ParseTable");
    check(p.runParserThrough(ParseStage::Drive), "⑤ 语法单步 Drive");
    check(p.parseStage() == ParseStage::Drive, "⑤ parseStage == Drive");
    check(p.currentStage() == JointStage::Parse, "⑤ 联合阶段推进到 Parse");
    check(p.snapshot().parse.parseTree != nullptr, "⑤ 语法树非空");
    check(p.runParserThrough(ParseStage::Ir), "⑤ 语法单步 Ir");
    check(p.currentStage() == JointStage::Ir, "⑤ 联合阶段推进到 Ir");
    check(static_cast<int>(p.snapshot().parse.ir.quads.size()) == 139, "⑤ 139 quads");

    std::filesystem::remove_all(tmp);
}

// ---- ④ 失效规则 ----
void testInvalidation(const std::string& langRoot)
{
    const auto tmp = makeTempDir("inval");
    const Inputs in = minicInputs(langRoot);

    FakeBackend backend;
    backend.lexText = makeSampleLex(in.rulesPath, in.srcPath, "MiniC");

    JointPipeline p(backend);
    p.setOutputDir((tmp / "gen").string());
    check(p.loadInputs(in.rulesPath, in.bnfPath, in.srcPath), "④ loadInputs 成功");
    check(p.runThrough(JointStage::Ir), "④ 首次全链路成功");

    // 相同 bnf 文本不失效
    p.setBnfText(loadFile(in.bnfPath));
    check(p.currentStage() == JointStage::Ir, "④ 相同 bnf 文本不失效");

    // bnf 变更：仅 Parse/Ir 失效，词法产物保留
    p.setBnfText(loadFile(in.bnfPath) + "\n# T3.4 失效测试追加注释\n");
    check(p.currentStage() == JointStage::LexRun, "④ bnf 变更 → 回退到 LexRun");
    JointSnapshot s = p.snapshot();
    check(!s.ok, "④ 失效后 snapshot.ok == false");
    check(!s.lex.runLexText.empty(), "④ 词法产物保留（runLexText 非空）");
    check(s.parse.parseTree == nullptr, "④ 旧语法树被清除");
    check(s.parse.ir.quads.empty(), "④ 旧 IR 被清除");

    check(p.runThrough(JointStage::Ir), "④ bnf 变更后重跑成功");
    check(static_cast<int>(p.snapshot().parse.ir.quads.size()) == 139, "④ 重跑后仍 139 quads");

    // 源程序变更：LexRun 起全部失效（词法+语法）
    const std::string src2 = (tmp / "test2.minic").string();
    {
        std::ofstream out(src2, std::ios::binary);
        out << "int a;\nint b;\nvoid main(void)\n{\n    a = b + 1;\n    return;\n}\n";
    }
    backend.lexText = makeSampleLex(in.rulesPath, src2, "MiniC");
    check(!backend.lexText.empty(), "④ 生成第二份 sample.lex");
    check(p.setSourcePath(src2), "④ setSourcePath 成功");
    check(p.currentStage() == JointStage::None, "④ 源程序变更 → 回退到 None");
    check(!p.strictLexOk(), "④ strictLexOk 复位");
    s = p.snapshot();
    check(s.lex.runLexText.empty(), "④ 词法 sample.lex 清除");
    check(s.parse.tokens.empty(), "④ 语法 token 流清除");
    check(s.parse.parseTree == nullptr, "④ 语法树清除");
    check(s.sampleLexPath.empty(), "④ sampleLexPath 清除");

    check(p.runThrough(JointStage::Ir), "④ 源程序变更后重跑成功");
    s = p.snapshot();
    check(static_cast<int>(s.parse.ir.quads.size()) < 139, "④ 新源程序 IR 四元式随代码量变化");
    check(s.parse.parseTree != nullptr, "④ 新源程序语法树非空");

    std::filesystem::remove_all(tmp);
}

// ---- ⑦ loadInputs 失败 + 停表回调 ----
void testLoadInputsFailureAndStop(const std::string& langRoot)
{
    const auto tmp = makeTempDir("misc");
    const Inputs in = minicInputs(langRoot);

    {
        FakeBackend backend;
        JointPipeline p(backend);
        p.setOutputDir((tmp / "gen").string());
        check(!p.loadInputs(in.rulesPath + ".nope", in.bnfPath, in.srcPath),
              "⑦ 缺 .rules → loadInputs 失败");
        check(hasDiag(p.snapshot(), "input", Severity::Error), "⑦ 诊断为 stage=input");
    }
    {
        FakeBackend backend;
        JointPipeline p(backend);
        p.setOutputDir((tmp / "gen2").string());
        check(!p.loadInputs(in.rulesPath, in.bnfPath, in.srcPath + ".nope"),
              "⑦ 缺源程序 → loadInputs 失败");
        check(hasDiag(p.snapshot(), "input", Severity::Error), "⑦ 源程序缺失诊断为 stage=input");
    }

    // 停表回调：阶段边界生效
    {
        FakeBackend backend;
        backend.lexText = makeSampleLex(in.rulesPath, in.srcPath, "MiniC");
        JointPipeline p(backend);
        p.setOutputDir((tmp / "gen3").string());
        bool stop = false;
        p.setStopCallback([&stop]() { return stop; });
        p.setStageCallback([](JointStage) {});
        p.setLexStageCallback([](Stage) {});
        p.setParseStageCallback([](ParseStage) {});
        check(p.loadInputs(in.rulesPath, in.bnfPath, in.srcPath), "⑦ loadInputs 成功");
        check(p.runThrough(JointStage::LexRun), "⑦ 停表前 LexRun 成功");
        stop = true;
        check(!p.runThrough(JointStage::Ir), "⑦ 停表后 runThrough(Ir) 返回 false");
        check(hasDiag(p.snapshot(), "joint", Severity::Info), "⑦ 诊断为 stage=joint（已取消）");
    }

    std::filesystem::remove_all(tmp);
}

// ---- ⑧ 预置 sample.lex 通道（独立发布环境，本机无 g++）----
void testPresetLex(const std::string& langRoot)
{
    const auto tmp = makeTempDir("preset");
    const Inputs in = minicInputs(langRoot);

    FakeBackend backend;
    backend.lexText = makeSampleLex(in.rulesPath, in.srcPath, "MiniC");
    check(!backend.lexText.empty(), "⑧ 生成测试用 sample.lex 文本");
    backend.failCompile = true;   // 模拟"本机没有 g++"：任何真实编译都会失败

    const std::filesystem::path lexDir = tmp / "gen" / "MiniC";
    std::filesystem::create_directories(lexDir);
    {
        std::ofstream out(lexDir / "sample.lex", std::ios::binary | std::ios::trunc);
        out << backend.lexText;
    }

    JointPipeline p(backend);
    p.setOutputDir((tmp / "gen").string());
    check(p.loadInputs(in.rulesPath, in.bnfPath, in.srcPath), "⑧ loadInputs 成功");

    check(p.loadPresetLexFromOutputDir(), "⑧ 自动定位并载入预置 sample.lex");
    JointSnapshot s = p.snapshot();
    check(s.presetLex, "⑧ 快照标记 presetLex");
    check(s.strictLexOk, "⑧ 预置产物满足语法前置");
    check(!s.lex.runLexText.empty(), "⑧ 预置文本已注入 runLexText");
    check(s.lex.previewLexText.empty(), "⑧ 未使用进程内预览");
    check(backend.compileCalls == 0, "⑧ 载入预置产物未调用编译器");

    check(p.runThrough(JointStage::Ir), "⑧ 预置产物可续跑语法与 IR");
    s = p.snapshot();
    check(s.parse.parseTree != nullptr, "⑧ 语法树非空");
    check(static_cast<int>(s.parse.ir.quads.size()) == 139, "⑧ 生成 139 条四元式");
    check(backend.compileCalls == 0, "⑧ 全程未调用编译器");

    // 源程序一改，预置产物必须作废：不能拿旧 token 流去装新程序
    p.setSourceText(loadFile(in.srcPath) + "\n\n");
    check(!p.presetLexUsed(), "⑧ 源程序变更后预置产物作废");
    check(p.currentStage() == JointStage::None, "⑧ 回退到 None");
    check(!p.strictLexOk(), "⑧ strictLexOk 复位");

    std::filesystem::remove_all(tmp);
}

} // namespace

int main(int argc, char** argv)
{
    std::string langRoot = "./test/languages";
    if (argc >= 2) {
        langRoot = argv[1];
    }

    testFullJointRun(langRoot);
    runStrictRejectCase(langRoot, true, "reject_compile");
    runStrictRejectCase(langRoot, false, "reject_run");
    testSingleStep(langRoot);
    testInvalidation(langRoot);
    testLoadInputsFailureAndStop(langRoot);
    testPresetLex(langRoot);

    std::cout << "checks=" << g_checks << " failures=" << g_failures << "\n";
    if (g_failures != 0) {
        std::cerr << "JointPipeline 单测失败\n";
        return 1;
    }
    std::cout << "JointPipeline 单测全部通过\n";
    return 0;
}
