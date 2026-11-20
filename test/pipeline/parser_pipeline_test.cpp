// T2.10 单测：ParserPipeline（任务二语法分析流水线，零 Qt）。
// 零 Qt：直接链 compiler_pipeline（+ grammar/parser/ir/lexer，经传递）。
// 编译：g++ -std=c++17 -O2 -Wall -Wextra（经 CTest / 手动）
// 运行：parser_pipeline_test <languages 根目录>（缺省 ./test/languages），退出码 0 = 通过
//
// 覆盖（docs/PROGRESS.md T2.10 锚点 + TASKS.md §4 验收）：
//   TINY / Mini-C 全链路（主 A：进程内扫描 test.<lang> → sample.lex 文本 → ParserPipeline）
//     LexRead → BnfParse → 化简 → First/Follow → LR(0) → LALR(1) → 分析表 → LR 驱动 → IR；
//     锚点：TINY 59 态 / 0 冲突 / 41 quads；Mini-C 128 态 / 1 个 S/R（s115/KW_ELSE）/ 139 quads；
//   降级路径：无 sample.lex 时 runThrough(ParseTable) 仍 ok（tokenCode=-1 + Info）；
//     runThrough(Drive) 无 sample.lex → 失败（stage="sample-lex-read"）；
//   失败路径：坏 .bnf → stage="bnf-parse"；
//   formatParseTree / formatIrProgram 冒烟。

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "LexerBuilder.h"
#include "LexRulesParser.h"
#include "ParserPipeline.h"
#include "SampleLexWriter.h"
#include "Scanner.h"

using compiler::Severity;
using compiler::SymbolKind;
using compiler::pipeline::ParseStage;
using compiler::pipeline::ParserPipeline;
using compiler::pipeline::ParserPipelineSnapshot;

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

// 进程内扫描 samples/test.<lang> → sample.lex 文本（主 A 输入，与 GUI 路径一致）
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

bool hasDiag(const ParserPipelineSnapshot& snap, const std::string& stage, Severity sev)
{
    for (const auto& d : snap.diagnostics) {
        if (d.stage == stage && d.severity == sev) {
            return true;
        }
    }
    return false;
}

void runLanguageCase(const std::string& langDir, const std::string& lang,
                     int expectedStates, int expectedConflicts, int expectedQuads)
{
    const std::string rulesPath = langDir + "/" + lang + ".rules";
    const std::string bnfPath = langDir + "/" + lang + ".bnf";
    const std::string srcPath = langDir + "/samples/test." +
                                (lang == "tiny" ? "tiny" : "minic");

    const std::string lexText = makeSampleLex(rulesPath, srcPath, lang);
    check(!lexText.empty(), ("生成 sample.lex 文本：" + lang).c_str());

    ParserPipeline pipeline;
    pipeline.setBnfText(loadFile(bnfPath));
    pipeline.setBnfPath(bnfPath);
    pipeline.setSampleLexText(lexText);

    const bool ok = pipeline.runThrough(ParseStage::Ir);
    check(ok, ("runThrough(Ir) ok：" + lang).c_str());

    const ParserPipelineSnapshot snap = pipeline.snapshot();
    check(snap.validThrough == ParseStage::Ir, ("validThrough==Ir：" + lang).c_str());
    check(snap.parseTree != nullptr, ("parseTree 非空：" + lang).c_str());
    check(static_cast<int>(snap.lr0.states.size()) == expectedStates,
          ("LR(0) 状态锚点：" + lang).c_str());
    check(static_cast<int>(snap.lalr1.states.size()) == expectedStates,
          ("LALR(1) 状态锚点：" + lang).c_str());
    check(static_cast<int>(snap.parseTable.conflicts.size()) == expectedConflicts,
          ("冲突数锚点：" + lang).c_str());
    check(static_cast<int>(snap.ir.quads.size()) == expectedQuads,
          ("IR 四元式锚点：" + lang).c_str());

    if (snap.parseTree && snap.parseTree->root) {
        check(snap.parseTree->root->symbolId == snap.simplifiedGrammar.startSymbol,
              ("语法树根 == 化简后开始符号：" + lang).c_str());
    }

    if (lang == "minic" && snap.parseTable.conflicts.size() == 1) {
        const auto& c = snap.parseTable.conflicts[0];
        check(c.state == 115 && c.type == "shift/reduce",
              "Mini-C 冲突锚点：s115 shift/reduce");
        const auto* term = snap.simplifiedGrammar.symbol(c.terminal);
        check(term != nullptr && term->name == "KW_ELSE",
              "Mini-C 冲突终结符锚点：KW_ELSE");
    }

    // 格式化冒烟
    const std::string treeText = compiler::pipeline::formatParseTree(*snap.parseTree);
    check(!treeText.empty(), ("formatParseTree 非空：" + lang).c_str());
    const std::string irText = compiler::pipeline::formatIrProgram(snap.ir);
    check(!irText.empty(), ("formatIrProgram 非空：" + lang).c_str());
}

void runDegradedPath(const std::string& langRoot)
{
    const std::string bnfPath = langRoot + "/tiny/tiny.bnf";
    const std::string bnfText = loadFile(bnfPath);

    // ① 全新管线直接 runThrough(Drive)：LexRead 硬失败（stage="sample-lex-read"，文档语义）
    {
        ParserPipeline p;
        p.setBnfText(bnfText);
        p.setBnfPath(bnfPath);
        const bool driveOk = p.runThrough(ParseStage::Drive);
        check(!driveOk, "降级：无 sample.lex 时全新管线 runThrough(Drive) 失败");
        const ParserPipelineSnapshot s = p.snapshot();
        check(hasDiag(s, "sample-lex-read", Severity::Error),
              "降级：全新管线 Drive 失败诊断为 stage=sample-lex-read");
    }

    // ② 降级推进到 ParseTable：仍 ok（tokenCode=-1 + Info 诊断）
    ParserPipeline pipeline;
    pipeline.setBnfText(bnfText);
    pipeline.setBnfPath(bnfPath);
    const bool tableOk = pipeline.runThrough(ParseStage::ParseTable);
    check(tableOk, "降级：无 sample.lex 时 runThrough(ParseTable) 仍 ok");
    const ParserPipelineSnapshot snap = pipeline.snapshot();
    check(snap.validThrough == ParseStage::ParseTable,
          "降级：validThrough == ParseTable");
    check(hasDiag(snap, "sample-lex-read", Severity::Info),
          "降级：存在 Info 诊断（未读取 sample.lex）");
    bool anyTerminalMinus1 = false;
    for (const auto& s : snap.simplifiedGrammar.symbols) {
        if (s.kind == SymbolKind::Terminal && s.tokenCode == -1) {
            anyTerminalMinus1 = true;
            break;
        }
    }
    check(anyTerminalMinus1, "降级：终结符 tokenCode == -1（registry=nullptr）");

    // ③ 降级推进后再 runThrough(Drive)：Drive 阶段失败（缺 token 流）
    const bool drive2 = pipeline.runThrough(ParseStage::Drive);
    check(!drive2, "降级：降级推进后再 runThrough(Drive) 失败");
}

void runBadBnfPath()
{
    ParserPipeline pipeline;
    pipeline.setBnfText("stmt -> ;");        // 产生式右部为空（非 #/ε）→ BnfParser 报错
    const bool ok = pipeline.runThrough(ParseStage::BnfParse);
    check(!ok, "坏 .bnf：runThrough(BnfParse) 失败");
    const ParserPipelineSnapshot snap = pipeline.snapshot();
    check(hasDiag(snap, "bnf-parse", Severity::Error),
          "坏 .bnf：诊断 stage == bnf-parse");
}

} // namespace

int main(int argc, char** argv)
{
    std::string langRoot = "./test/languages";
    if (argc >= 2) {
        langRoot = argv[1];
    }

    runLanguageCase(langRoot + "/tiny", "tiny", 59, 0, 41);
    runLanguageCase(langRoot + "/minic", "minic", 128, 1, 139);
    runDegradedPath(langRoot);
    runBadBnfPath();

    if (g_failures != 0) {
        std::cerr << "[FAILED] " << g_failures << " of " << g_checks << " checks\n";
        return 1;
    }
    std::cout << "[OK] " << g_checks << " checks passed\n";
    return 0;
}
