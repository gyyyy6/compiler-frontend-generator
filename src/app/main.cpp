#include <QApplication>
#include "MainWindow.h"

#include <cstddef>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "LexerBuilder.h"
#include "LexRulesParser.h"
#include "SampleLexWriter.h"
#include "Scanner.h"

#ifdef COMPILER_ENABLE_TASK2
#include <QCoreApplication>

#include <filesystem>

#include "CompilerRunnerBackend.h"
#include "JointPipeline.h"
#include "ParserPipeline.h"
#include "Task3PipelineWorker.h"
#endif

#ifdef COMPILER_ENABLE_TASK2
namespace {

namespace fs = std::filesystem;

// 语言包目录内首个指定扩展名文件（<lang>.rules/<lang>.bnf 不存在时的回退）
std::string firstFileWithExtension(const std::string& dir, const std::string& ext)
{
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) {
            continue;
        }
        if (it->path().extension().string() == ext) {
            return it->path().string();
        }
    }
    return {};
}

bool writeTextFile(const std::string& path, const std::string& data)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << data;
    return out.good();
}

} // namespace
#endif

int main(int argc, char* argv[])
{
    // ---- CLI 模式（ARCHITECTURE.md I7，T1.8 实现）：--lex <rules> <source> [output] ----
    // 不构造 QApplication，直接用 ifstream/ofstream 直读直写，走完 return。
    if (argc >= 2 && std::string(argv[1]) == "--lex") {
        if (argc < 4 || argc > 5) {
            std::cerr << "usage: CompilerCourseApp --lex <rules> <source> [output]\n";
            return 2;
        }
        const std::string rulesPath = argv[2];
        const std::string sourcePath = argv[3];
        const std::string outputPath = (argc >= 5) ? argv[4] : "sample.lex";

        // 1) 解析 .rules
        compiler::lexer::LexRulesParser parser;
        auto parsed = parser.parseFile(rulesPath);
        if (!parsed.ok) {
            for (const auto& d : parsed.diagnostics) {
                std::cerr << "[rules-parse] " << d.message << "\n";
            }
            return 3;
        }
        compiler::lexer::LexerSpec spec = std::move(parsed.value);

        // 2) 构建最小 DFA
        compiler::lexer::LexerBuilder builder;
        auto dfaRes = builder.buildMinimalDfa(spec);
        if (!dfaRes.ok) {
            for (const auto& d : dfaRes.diagnostics) {
                std::cerr << "[" << d.stage << "] " << d.message << "\n";
            }
            return 3;
        }

        // 3) 扫描
        compiler::lexer::Scanner scanner;
        if (!scanner.configure(spec, dfaRes.value)) {
            for (const auto& d : scanner.configDiagnostics()) {
                std::cerr << "[scan] " << d.message << "\n";
            }
            return 3;
        }
        std::ifstream srcIn(sourcePath, std::ios::binary);
        if (!srcIn) {
            std::cerr << "[scan] 无法打开源文件: " << sourcePath << "\n";
            return 4;
        }
        const std::string source((std::istreambuf_iterator<char>(srcIn)),
                                 std::istreambuf_iterator<char>());
        compiler::lexer::ScanResult scanRes = scanner.scan(source, sourcePath);

        // 4) 渲染 sample.lex（渲染单点化：SampleLexWriter）
        const std::string text = compiler::lexer::SampleLexWriter::render(
            scanRes.registry, spec.name, sourcePath, scanRes.tokens);
        std::ofstream out(outputPath, std::ios::binary);
        if (!out) {
            std::cerr << "[scan] 无法打开输出文件: " << outputPath << "\n";
            return 4;
        }
        out << text;
        out.close();

        // 5) 扫描诊断打到 stderr（多错误收集，不影响输出文件）
        for (const auto& d : scanRes.diagnostics) {
            std::cerr << "[" << d.stage << "] " << d.message << "\n";
        }
        return 0;
    }

#ifdef COMPILER_ENABLE_TASK2
    // ---- CLI 模式（ARCHITECTURE.md I7，T2.10 实现）：--parse <bnf> <sample.lex> [output-ir] ----
    // 不构造 QApplication，复用进程内 ParserPipeline（决策点 4，不生成独立解析器），
    // 直读直写走完 return。输出：语法树文本 → stdout；IR 四元式 → 文件（可选）或 stdout。
    if (argc >= 2 && std::string(argv[1]) == "--parse") {
        if (argc < 4 || argc > 5) {
            std::cerr << "usage: CompilerCourseApp --parse <bnf> <sample.lex> [output-ir]\n";
            return 2;
        }
        const std::string bnfPath = argv[2];
        const std::string lexPath = argv[3];
        const std::string irPath = (argc >= 5) ? argv[4] : std::string();

        compiler::pipeline::ParserPipeline pipeline;

        {
            std::ifstream bnfIn(bnfPath, std::ios::binary);
            if (!bnfIn) {
                std::cerr << "[bnf-parse] 无法打开 BNF 文件: " << bnfPath << "\n";
                return 4;
            }
            const std::string bnfText((std::istreambuf_iterator<char>(bnfIn)),
                                      std::istreambuf_iterator<char>());
            pipeline.setBnfText(bnfText);
            pipeline.setBnfPath(bnfPath);
        }
        {
            std::ifstream lexIn(lexPath, std::ios::binary);
            if (!lexIn) {
                std::cerr << "[sample-lex-read] 无法打开 sample.lex 文件: " << lexPath << "\n";
                return 4;
            }
            const std::string lexText((std::istreambuf_iterator<char>(lexIn)),
                                      std::istreambuf_iterator<char>());
            pipeline.setSampleLexText(lexText);
            pipeline.setSampleLexPath(lexPath);
        }

        const bool ok = pipeline.runThrough(compiler::pipeline::ParseStage::Ir);
        for (const auto& d : pipeline.diagnostics()) {
            std::cerr << "[" << d.stage << "] " << d.message << "\n";
        }
        if (!ok) {
            return 3;
        }

        const compiler::pipeline::ParserPipelineSnapshot snap = pipeline.snapshot();
        if (snap.parseTree) {
            std::cout << compiler::pipeline::formatParseTree(*snap.parseTree);
        }
        const std::string irText = compiler::pipeline::formatIrProgram(snap.ir);
        if (!irPath.empty()) {
            std::ofstream out(irPath, std::ios::binary);
            if (!out) {
                std::cerr << "[ir] 无法打开输出文件: " << irPath << "\n";
                return 4;
            }
            out << irText;
            out.close();
        } else {
            std::cout << irText;
        }
        return 0;
    }
#endif

#ifdef COMPILER_ENABLE_TASK2
    // ---- CLI 模式（ARCHITECTURE.md I7 / §10，T3.5 实现）----
    // --minic <langDir> <source> [outputDir]
    // 复用 JointPipeline（强制真实生成扫描器）与 Task3PipelineWorker 的报告渲染；
    // 产物：sample.lex（流水线写入 outputDir/<lang>/）、<source>.tree.txt、
    //       <source>.ir.txt、<source>.report.md。
    // 退出码：0 全链成功；2 用法错误；3 词法/语法（必需阶段）失败；4 I/O 失败；
    //         5 词法+语法成功但 IR 失败（报告标注）。
    if (argc >= 2 && std::string(argv[1]) == "--minic") {
        if (argc < 4 || argc > 5) {
            std::cerr << "usage: CompilerCourseApp --minic <langDir> <source> [outputDir]\n";
            return 2;
        }
        const std::string langDir = argv[2];
        const std::string sourcePath = argv[3];
        const std::string outputDir = (argc >= 5) ? argv[4] : "build/generated";

        // QProcess（CompilerRunnerBackend）需要 Qt 事件分发基础设施；本分支不建 GUI
        QCoreApplication coreApp(argc, argv);

        std::error_code ec;
        if (!fs::is_directory(langDir, ec)) {
            std::cerr << "[input] 语言包目录不存在: " << langDir << "\n";
            return 4;
        }
        if (!fs::is_regular_file(sourcePath, ec)) {
            std::cerr << "[input] 源程序不存在: " << sourcePath << "\n";
            return 4;
        }

        const std::string base = fs::path(langDir).filename().string();
        std::string rulesPath = (fs::path(langDir) / (base + ".rules")).string();
        std::string bnfPath = (fs::path(langDir) / (base + ".bnf")).string();
        if (!fs::exists(rulesPath, ec)) {
            rulesPath = firstFileWithExtension(langDir, ".rules");
        }
        if (!fs::exists(bnfPath, ec)) {
            bnfPath = firstFileWithExtension(langDir, ".bnf");
        }
        if (rulesPath.empty() || bnfPath.empty()) {
            std::cerr << "[input] 语言包缺少 .rules 或 .bnf: " << langDir << "\n";
            return 4;
        }

        compiler::gui::CompilerRunnerBackend backend;
        compiler::pipeline::JointPipeline pipeline(backend);
        pipeline.setOutputDir(outputDir);

        if (!pipeline.loadInputs(rulesPath, bnfPath, sourcePath)) {
            for (const auto& d : pipeline.diagnostics()) {
                std::cerr << "[" << d.stage << "] " << d.message << "\n";
            }
            std::cerr << "[input] 读取输入失败\n";
            return 4;
        }

        // 必需阶段（词法真实产出 + 语法树）与选做转必做阶段（IR）分开推进，
        // 便于 IR 失败时仍能归档语法树与报告。
        const bool parseOk = pipeline.runThrough(compiler::pipeline::JointStage::Parse);
        bool irOk = false;
        if (parseOk) {
            irOk = pipeline.runThrough(compiler::pipeline::JointStage::Ir);
        }

        const compiler::pipeline::JointSnapshot snap = pipeline.snapshot();
        for (const auto& d : snap.diagnostics) {
            std::cerr << "[" << d.stage << "] " << d.message << "\n";
        }

        const std::string langName = snap.languageName.empty() ? base : snap.languageName;
        const fs::path artifactsDir = fs::path(outputDir) / langName;
        fs::create_directories(artifactsDir, ec);
        if (ec) {
            std::cerr << "[output] 无法创建产物目录: " << artifactsDir.string() << "\n";
            return 4;
        }

        const std::string fileName = fs::path(sourcePath).filename().string();
        const std::string treePath = (artifactsDir / (fileName + ".tree.txt")).string();
        const std::string irPath = (artifactsDir / (fileName + ".ir.txt")).string();
        const std::string reportPath = (artifactsDir / (fileName + ".report.md")).string();

        if (snap.parse.parseTree) {
            if (!writeTextFile(treePath,
                               compiler::pipeline::formatParseTree(*snap.parse.parseTree))) {
                std::cerr << "[output] 写入语法树失败: " << treePath << "\n";
                return 4;
            }
        }
        if (!snap.parse.ir.quads.empty()) {
            if (!writeTextFile(irPath,
                               compiler::pipeline::formatIrProgram(snap.parse.ir))) {
                std::cerr << "[output] 写入 IR 失败: " << irPath << "\n";
                return 4;
            }
        }

        compiler::gui::JointReportContext context;
        context.rulesPath = rulesPath;
        context.bnfPath = bnfPath;
        context.sourcePath = sourcePath;
        context.outputDir = outputDir;
        if (!writeTextFile(reportPath,
                           compiler::gui::formatJointReportMarkdown(snap, context))) {
            std::cerr << "[output] 写入报告失败: " << reportPath << "\n";
            return 4;
        }

        const std::size_t tokenCount = compiler::gui::jointTokens(snap).size();
        std::cout << "[joint] stage="
                  << compiler::pipeline::jointStageName(snap.validThrough)
                  << " ok=" << (snap.ok ? "true" : "false")
                  << " language=" << langName
                  << " tokens=" << tokenCount
                  << " lr0=" << snap.parse.lr0.states.size()
                  << " lalr1=" << snap.parse.lalr1.states.size()
                  << " conflicts=" << snap.parse.parseTable.conflicts.size()
                  << " quads=" << snap.parse.ir.quads.size()
                  << " runProducedLex=" << (snap.runProducedLex ? "true" : "false")
                  << "\n";
        if (!snap.sampleLexPath.empty()) {
            std::cout << "[artifact] sample.lex: " << snap.sampleLexPath << "\n";
        }
        if (snap.parse.parseTree) {
            std::cout << "[artifact] tree: " << treePath << "\n";
        }
        if (!snap.parse.ir.quads.empty()) {
            std::cout << "[artifact] ir: " << irPath << "\n";
        }
        std::cout << "[artifact] report: " << reportPath << "\n";

        if (!parseOk) {
            return 3;
        }
        if (!irOk) {
            std::cerr << "[ir] 中间代码阶段失败（词法/语法产物已归档）\n";
            return 5;
        }
        return 0;
    }
#endif

    QApplication app(argc, argv);

    QApplication::setOrganizationName(QStringLiteral("CompilerCourse"));
    QApplication::setApplicationName(QStringLiteral("CompilerCourseApp"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    compiler::gui::MainWindow window;
    window.show();

    return QApplication::exec();
}
