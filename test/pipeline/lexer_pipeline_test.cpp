// LexerPipeline 无 Qt 单测（T1.9）
// 零 Qt：直接链 compiler_pipeline（core/regex/lexer/codegen），fake CompileRunBackend。
// 编译：g++ -std=c++17 -O2 -Wall -Wextra（经 CTest / 手动）
// 运行：lexer_pipeline_test（退出码 0 = 全部通过）

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "LexerPipeline.h"

using compiler::Diagnostic;
using compiler::Severity;
using compiler::pipeline::CompileRunBackend;
using compiler::pipeline::LexerPipeline;
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

void expectContains(const std::string& haystack, const std::string& needle,
                    const char* what)
{
    check(haystack.find(needle) != std::string::npos, what);
}

bool hasDiag(const PipelineSnapshot& snap, const std::string& stage, Severity sev)
{
    for (const Diagnostic& d : snap.diagnostics) {
        if (d.stage == stage && d.severity == sev) {
            return true;
        }
    }
    return false;
}

// ---- 假后端：compile 恒成功（生成占位 exe）；run 按配置失败或写假 sample.lex ----
class FakeBackend : public CompileRunBackend {
public:
    bool failCompile = false;
    bool failRun = false;

    bool compile(const std::string& sourceFile, const std::string& outputExe,
                 std::string& errorOut) override
    {
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
        (void)exe;
        (void)stdOut;
        if (failRun) {
            stdErr = "fake: scanner crashed";
            exitCode = 1;
            return false;
        }
        const std::string outPath = args.at(1);
        std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
        out << "#file-format: sample.lex\n"
            << "#version: 1\n"
            << "#language: FAKE\n"
            << "#token-table-end\n"
            << "1 2 3\n";
        exitCode = 0;
        return out.good();
    }
};

// ---- 内嵌 TINY 规则（与 test/tiny.rules 同风格：显式编码、关键字在 ID 之前）----
const char* kTinyRules = R"(
@name TINY
@case-sensitive false
@ignore [ \t\r\n]
@comment block { }
KW_IF_103 = IF
KW_THEN_104 = THEN
KW_ELSE_105 = ELSE
KW_END_106 = END
KW_REPEAT_107 = REPEAT
KW_UNTIL_108 = UNTIL
KW_READ_109 = READ
KW_WRITE_110 = WRITE
ID_101 = letter (letter|digit)*
Num_100 = digit+
ASSIGN_200 = :=
PLUS_201 = \+
MINUS_202 = -
MUL_203 = \*
DIV_204 = /
LT_205 = <
LE_206 = <=
GT_207 = >
GE_208 = >=
EQ_209 = =
LPAREN_210 = \(
RPAREN_211 = \)
SEMI_212 = ;
)";

const char* kTinySource = "if then x := 123 ;";
const char* kExpectedDataLine = "103 104 101 \"x\" 200 100 123 212";

std::filesystem::path makeTempDir(const char* tag)
{
    const auto base = std::filesystem::temp_directory_path() /
        ("compiler_pipeline_test_" + std::string(tag));
    std::filesystem::remove_all(base);
    std::filesystem::create_directories(base);
    return base;
}

// ---- 用例 ----
void testFullRun()
{
    const auto tmp = makeTempDir("full");
    FakeBackend backend;
    LexerPipeline p(backend);
    p.setOutputDir((tmp / "gen").string());
    p.setRulesText(kTinyRules);
    const std::string srcPath = (tmp / "sample.tny").string();
    {
        std::ofstream out(srcPath, std::ios::binary);
        out << kTinySource;
    }
    check(p.setSourcePath(srcPath), "setSourcePath 成功读取源程序");

    check(p.runThrough(Stage::Run), "runThrough(Run) 全流程成功");
    check(p.currentStage() == Stage::Run, "currentStage == Run");

    const PipelineSnapshot snap = p.snapshot();
    check(snap.ok, "snapshot.ok == true");
    check(snap.runProducedLex, "runProducedLex == true（真实运行）");
    expectContains(snap.runLexText, "#file-format: sample.lex", "runLexText 含 file-format");
    expectContains(snap.runLexText, "#language: FAKE", "runLexText 为假后端输出");
    check(!snap.nfa.states.empty(), "NFA 非空");
    check(!snap.dfa.states.empty(), "DFA 非空");
    check(!snap.minimalDfa.states.empty(), "最小DFA 非空");
    check(snap.minimalDfa.states.size() <= snap.dfa.states.size(),
          "最小DFA 状态数 ≤ DFA");
    check(!snap.generatedSource.empty(), "生成源码非空");
    expectContains(snap.generatedSource, "int main", "生成源码含 int main");
    expectContains(snap.generatedSource, "START_STATE", "生成源码含 START_STATE");
    check(hasDiag(snap, "compile", Severity::Info), "含 compile Info 诊断");

    std::filesystem::remove_all(tmp);
}

void testPreviewFallbackOnCompileFailure()
{
    const auto tmp = makeTempDir("preview");
    FakeBackend backend;
    backend.failCompile = true;
    LexerPipeline p(backend);
    p.setOutputDir((tmp / "gen").string());
    p.setRulesText(kTinyRules);
    p.setSourceText(kTinySource);

    check(!p.runThrough(Stage::Run), "编译失败 → runThrough(Run) 返回 false");
    const PipelineSnapshot snap = p.snapshot();
    check(!snap.ok, "snapshot.ok == false");
    check(!snap.runProducedLex, "runProducedLex == false（非真实运行）");
    check(!snap.previewLexText.empty(), "预览回退非空");
    expectContains(snap.previewLexText, "#language: TINY", "预览含 #language: TINY");
    expectContains(snap.previewLexText, "#token 103 KW_IF KW_IF",
                  "预览头部 token 表含 KW_IF");
    expectContains(snap.previewLexText, kExpectedDataLine, "预览数据行正确");
    check(hasDiag(snap, "compile", Severity::Error), "含 compile Error 诊断");
    check(snap.validThrough == Stage::Codegen, "validThrough == Codegen（编译失败处）");

    std::filesystem::remove_all(tmp);
}

void testRunFailureFallback()
{
    const auto tmp = makeTempDir("runfail");
    FakeBackend backend;
    backend.failRun = true;
    LexerPipeline p(backend);
    p.setOutputDir((tmp / "gen").string());
    p.setRulesText(kTinyRules);
    p.setSourceText(kTinySource);

    check(!p.runThrough(Stage::Run), "运行失败 → runThrough(Run) 返回 false");
    const PipelineSnapshot snap = p.snapshot();
    check(!snap.runProducedLex, "runProducedLex == false");
    check(!snap.previewLexText.empty(), "预览回退非空");
    check(hasDiag(snap, "run", Severity::Error), "含 run Error 诊断");

    std::filesystem::remove_all(tmp);
}

void testRulesParseFailure()
{
    FakeBackend backend;
    LexerPipeline p(backend);
    p.setOutputDir("unused");
    p.setRulesText("this line has no equals sign");
    check(!p.runThrough(Stage::Rules), "坏规则 → Rules 失败");
    const PipelineSnapshot snap = p.snapshot();
    check(hasDiag(snap, "rules-parse", Severity::Error), "含 rules-parse Error 诊断");
}

void testInvalidation()
{
    const auto tmp = makeTempDir("inval");
    FakeBackend backend;
    LexerPipeline p(backend);
    p.setOutputDir((tmp / "gen").string());
    p.setRulesText(kTinyRules);
    p.setSourceText(kTinySource);

    check(p.runThrough(Stage::Run), "全流程成功");
    check(p.currentStage() == Stage::Run, "currentStage == Run");

    // 相同内容不失效
    p.setRulesText(kTinyRules);
    check(p.currentStage() == Stage::Run, "相同规则文本不失效");

    // 源程序变更：仅 Run 级失效，上游保留
    p.setSourceText("x := 1 ;");
    check(p.currentStage() == Stage::Compile, "源程序变更后 currentStage == Compile");
    check(p.snapshot().validThrough == Stage::Compile, "validThrough == Compile");
    check(!p.snapshot().minimalDfa.states.empty(), "源程序变更后最小DFA 保留");

    // 规则变更：全部失效
    p.setRulesText("ID_1 = letter+");
    check(p.currentStage() == Stage::None, "规则变更后 currentStage == None");
    check(p.snapshot().nfa.states.empty(), "规则变更后 NFA 清空");

    std::filesystem::remove_all(tmp);
}

void testPartialRun()
{
    FakeBackend backend;
    LexerPipeline p(backend);
    p.setOutputDir("unused");
    p.setRulesText(kTinyRules);

    check(p.runThrough(Stage::Nfa), "只生成 NFA 成功");
    const PipelineSnapshot snap = p.snapshot();
    check(snap.validThrough == Stage::Nfa, "validThrough == Nfa");
    check(!snap.nfa.states.empty(), "NFA 非空");
    check(snap.dfa.states.empty(), "DFA 未构建");
    check(snap.generatedSource.empty(), "生成源码未构建");
}

} // namespace

int main()
{
    testFullRun();
    testPreviewFallbackOnCompileFailure();
    testRunFailureFallback();
    testRulesParseFailure();
    testInvalidation();
    testPartialRun();

    std::cout << "checks=" << g_checks << " failures=" << g_failures << "\n";
    if (g_failures != 0) {
        std::cerr << "LexerPipeline 单测失败\n";
        return 1;
    }
    std::cout << "LexerPipeline 单测全部通过\n";
    return 0;
}
