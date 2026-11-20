#include "LexerPipeline.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

#include "LexerBuilder.h"
#include "LexRulesParser.h"
#include "MinimizeDfa.h"
#include "NfaToDfa.h"
#include "SampleLexWriter.h"
#include "Scanner.h"
#include "ScannerSourceGenerator.h"

namespace compiler::pipeline {
namespace {

#ifdef _WIN32
constexpr const char* kExeSuffix = ".exe";
#else
constexpr const char* kExeSuffix = "";
#endif

// 去除 UTF-8 BOM（若存在）
std::string stripBom(const std::string& s)
{
    if (s.size() >= 3 &&
        static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB &&
        static_cast<unsigned char>(s[2]) == 0xBF) {
        return s.substr(3);
    }
    return s;
}

} // namespace

LexerPipeline::LexerPipeline(CompileRunBackend& backend)
    : backend_(backend)
{
}

void LexerPipeline::setRulesText(const std::string& text)
{
    const std::string t = stripBom(text);
    if (t == rulesText_) {
        return;   // 内容未变，不失效
    }
    rulesText_ = t;
    invalidateFrom(Stage::Rules);
}

void LexerPipeline::setRulesPath(const std::string& path)
{
    rulesPath_ = path;
}

void LexerPipeline::setSourceText(const std::string& text)
{
    const std::string t = stripBom(text);
    if (t == sourceText_) {
        return;
    }
    sourceText_ = t;
    invalidateFrom(Stage::Run);   // 仅 sample.lex（含预览）失效，上游保留
}

bool LexerPipeline::setSourcePath(const std::string& path)
{
    std::string text;
    std::string err;
    if (!readFile(path, text, err)) {
        addDiag(Severity::Error, "input", "读取源程序文件失败：" + err);
        return false;
    }
    sourcePath_ = path;
    setSourceText(text);
    return true;
}

void LexerPipeline::setOutputDir(const std::string& dir)
{
    if (dir != outputDir_) {
        outputDir_ = dir;
        // 产物路径变化：编译/运行结果失效，上游保留
        invalidateFrom(Stage::Compile);
    }
}

void LexerPipeline::setStopCallback(std::function<bool()> cb)
{
    stopCb_ = std::move(cb);
}

void LexerPipeline::setStageCallback(std::function<void(Stage)> cb)
{
    stageCb_ = std::move(cb);
}

bool LexerPipeline::runThrough(Stage target)
{
    diags_.clear();
    if (static_cast<int>(target) < static_cast<int>(Stage::Rules)) {
        lastRunOk_ = true;
        return true;   // 无事可做（GUI 不会传 None）
    }
    if (stopRequested()) {
        addDiag(Severity::Info, "pipeline", "已取消");
        lastRunOk_ = false;
        return false;
    }

    const int end = static_cast<int>(target);
    for (int s = static_cast<int>(currentStage_) + 1; s <= end; ++s) {
        if (stopRequested()) {
            addDiag(Severity::Info, "pipeline", "已取消");
            lastRunOk_ = false;
            return false;
        }
        const Stage st = static_cast<Stage>(s);
        if (stageCb_) {
            stageCb_(st);
        }

        bool ok = false;
        switch (st) {
        case Stage::Rules:    ok = runStageRules();    break;
        case Stage::Nfa:      ok = runStageNfa();      break;
        case Stage::Dfa:      ok = runStageDfa();      break;
        case Stage::Minimize: ok = runStageMinimize(); break;
        case Stage::Codegen:  ok = runStageCodegen();  break;
        case Stage::Compile:  ok = runStageCompile();  break;
        case Stage::Run:      ok = runStageRun();      break;
        default:              ok = false;              break;
        }
        if (!ok) {
            lastRunOk_ = false;
            return false;
        }
        currentStage_ = st;
    }
    lastRunOk_ = true;
    return true;
}

bool LexerPipeline::runStageRules()
{
    if (rulesText_.empty()) {
        addDiag(Severity::Error, "rules-parse", "未加载 .rules 规则文件");
        return false;
    }
    lexer::LexRulesParser parser;
    auto res = parser.parse(rulesText_);
    for (auto& d : res.diagnostics) {
        diags_.push_back(std::move(d));
    }
    if (!res.ok) {
        return false;
    }
    spec_ = std::move(res.value);
    addDiag(Severity::Info, "rules-parse",
            "规则解析完成：" + std::to_string(spec_.rules.size()) + " 条规则");
    return true;
}

bool LexerPipeline::runStageNfa()
{
    lexer::LexerBuilder builder;
    auto res = builder.buildNfa(spec_);
    for (auto& d : res.diagnostics) {
        diags_.push_back(std::move(d));
    }
    if (!res.ok) {
        return false;
    }
    nfa_ = std::move(res.value);
    addDiag(Severity::Info, "nfa-build",
            "NFA 构建完成：" + std::to_string(nfa_.states.size()) + " 个状态");
    return true;
}

bool LexerPipeline::runStageDfa()
{
    auto res = regex::subsetConstruct(nfa_);
    for (auto& d : res.diagnostics) {
        diags_.push_back(std::move(d));
    }
    if (!res.ok) {
        return false;
    }
    dfa_ = std::move(res.value);
    addDiag(Severity::Info, "dfa-build",
            "DFA 构建完成：" + std::to_string(dfa_.states.size()) + " 个状态");
    return true;
}

bool LexerPipeline::runStageMinimize()
{
    auto res = regex::minimizeDfa(dfa_);
    for (auto& d : res.diagnostics) {
        diags_.push_back(std::move(d));
    }
    if (!res.ok) {
        return false;
    }
    minimalDfa_ = std::move(res.value);
    addDiag(Severity::Info, "dfa-minimize",
            "DFA 最小化完成：" + std::to_string(dfa_.states.size()) + " → "
            + std::to_string(minimalDfa_.states.size()) + " 个状态");
    return true;
}

bool LexerPipeline::runStageCodegen()
{
    codegen::ScannerSourceGenerator generator;
    auto res = generator.generate(spec_, minimalDfa_);
    for (auto& d : res.diagnostics) {
        diags_.push_back(std::move(d));
    }
    if (!res.ok) {
        return false;
    }
    generatedSource_ = std::move(res.value);
    addDiag(Severity::Info, "codegen", "扫描器源码生成完成");
    return true;
}

bool LexerPipeline::runStageCompile()
{
    if (generatedSource_.empty()) {
        addDiag(Severity::Error, "compile", "生成源码为空，无法编译");
        return false;
    }
    if (!ensureLangDir()) {
        return false;
    }
    const std::string cppPath = langDir() + "/generated_lexer.cpp";
    std::string err;
    if (!writeFile(cppPath, generatedSource_, err)) {
        addDiag(Severity::Error, "compile", "写入生成源码失败：" + err);
        return false;
    }

    exePath_ = langDir() + "/" + languageName() + "_scanner" + kExeSuffix;
    std::string errorOut;
    if (!backend_.compile(cppPath, exePath_, errorOut)) {
        addDiag(Severity::Error, "compile",
                errorOut.empty() ? "编译失败（g++ 无输出）" : errorOut);
        addDiag(Severity::Warning, "compile",
                "编译失败：sample.lex 将回退为进程内预览（非真实运行结果）");
        buildPreviewLex();
        return false;
    }
    addDiag(Severity::Info, "compile", "编译成功：" + exePath_);
    return true;
}

bool LexerPipeline::runStageRun()
{
    runProducedLex_ = false;
    runLexText_.clear();
    previewLexText_.clear();
    if (sourceText_.empty()) {
        addDiag(Severity::Error, "run", "未加载源程序文件");
        return false;
    }
    const std::string inPath = ensureSourceFile();
    if (inPath.empty()) {
        addDiag(Severity::Error, "run", "无法准备源程序输入文件");
        return false;
    }

    const std::string outPath = langDir() + "/sample.lex";
    std::string stdOut, stdErr;
    int exitCode = -1;
    const bool launched = backend_.run(exePath_, {inPath, outPath}, stdOut, stdErr, exitCode);
    const std::string so = trim(stdOut);
    if (!so.empty()) {
        addDiag(Severity::Info, "run", so);
    }

    if (launched && exitCode == 0) {
        std::string text;
        std::string err;
        if (readFile(outPath, text, err)) {
            runLexText_ = std::move(text);
            runProducedLex_ = true;
            addDiag(Severity::Info, "run", "扫描完成，sample.lex 已写入 " + outPath);
            return true;
        }
        addDiag(Severity::Error, "run", "回读 sample.lex 失败：" + err);
    } else {
        addDiag(Severity::Error, "run",
                "运行扫描器失败（退出码 " + std::to_string(exitCode) + "）：" + trim(stdErr));
    }

    addDiag(Severity::Warning, "run",
            "sample.lex 回退为进程内预览（非真实运行结果）");
    buildPreviewLex();
    return false;
}

bool LexerPipeline::buildPreviewLex()
{
    previewLexText_.clear();
    if (sourceText_.empty()) {
        return false;
    }
    if (static_cast<int>(currentStage_) < static_cast<int>(Stage::Minimize)) {
        return false;
    }

    lexer::Scanner scanner;
    if (!scanner.configure(spec_, minimalDfa_)) {
        for (const auto& d : scanner.configDiagnostics()) {
            diags_.push_back(d);
        }
        return false;
    }
    const auto res = scanner.scan(sourceText_, sourcePath_);
    previewLexText_ = lexer::SampleLexWriter::render(
        res.registry, languageName(), sourcePath_, res.tokens);
    if (!previewLexText_.empty()) {
        addDiag(Severity::Info, "scan",
                "进程内预览渲染完成（" + std::to_string(res.tokens.size())
                + " 个 token，含 EOF）");
    }
    return !previewLexText_.empty();
}

void LexerPipeline::invalidateFrom(Stage from)
{
    const int fromIdx = static_cast<int>(from);
    if (fromIdx > static_cast<int>(currentStage_)) {
        return;   // 该阶段之后本来就无有效产物
    }
    const auto clearRun = [this]() {
        runLexText_.clear();
        previewLexText_.clear();
        runProducedLex_ = false;
    };
    switch (from) {
    case Stage::Run:
        clearRun();
        break;
    case Stage::Compile:
        exePath_.clear();
        clearRun();
        break;
    case Stage::Codegen:
        generatedSource_.clear();
        exePath_.clear();
        clearRun();
        break;
    case Stage::Minimize:
        minimalDfa_ = Dfa{};
        generatedSource_.clear();
        exePath_.clear();
        clearRun();
        break;
    case Stage::Dfa:
        dfa_ = Dfa{};
        minimalDfa_ = Dfa{};
        generatedSource_.clear();
        exePath_.clear();
        clearRun();
        break;
    case Stage::Nfa:
        nfa_ = Nfa{};
        dfa_ = Dfa{};
        minimalDfa_ = Dfa{};
        generatedSource_.clear();
        exePath_.clear();
        clearRun();
        break;
    case Stage::Rules:
    case Stage::None:
        spec_ = lexer::LexerSpec{};
        nfa_ = Nfa{};
        dfa_ = Dfa{};
        minimalDfa_ = Dfa{};
        generatedSource_.clear();
        exePath_.clear();
        clearRun();
        break;
    }
    currentStage_ = static_cast<Stage>(fromIdx - 1);
}

bool LexerPipeline::stopRequested()
{
    return stopCb_ && stopCb_();
}

void LexerPipeline::addDiag(Severity sev, const std::string& stage,
                            const std::string& msg, int line)
{
    Diagnostic d;
    d.severity = sev;
    d.stage = stage;
    d.message = msg;
    d.loc.line = line;
    d.loc.column = -1;
    d.loc.offset = -1;
    diags_.push_back(std::move(d));
}

PipelineSnapshot LexerPipeline::snapshot() const
{
    PipelineSnapshot snap;
    snap.ok = lastRunOk_;
    snap.runProducedLex = runProducedLex_;
    snap.validThrough = currentStage_;
    snap.nfa = nfa_;
    snap.dfa = dfa_;
    snap.minimalDfa = minimalDfa_;
    snap.generatedSource = generatedSource_;
    snap.previewLexText = previewLexText_;
    snap.runLexText = runLexText_;
    snap.languageName = languageName();
    snap.exePath = exePath_;
    snap.diagnostics = diags_;
    return snap;
}

std::string LexerPipeline::languageName() const
{
    if (!spec_.name.empty()) {
        return spec_.name;
    }
    // 规则文件主干（去掉目录与扩展名）
    const std::size_t slash = rulesPath_.find_last_of("/\\");
    std::string base = (slash == std::string::npos) ? rulesPath_ : rulesPath_.substr(slash + 1);
    const std::size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) {
        base = base.substr(0, dot);
    }
    return base.empty() ? std::string("lang") : base;
}

std::string LexerPipeline::langDir() const
{
    std::string dir = outputDir_;
    while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\')) {
        dir.pop_back();
    }
    return dir + "/" + languageName();
}

bool LexerPipeline::ensureLangDir()
{
    std::error_code ec;
    std::filesystem::create_directories(langDir(), ec);
    if (ec) {
        addDiag(Severity::Error, "compile", "创建输出目录失败：" + ec.message());
        return false;
    }
    return true;
}

std::string LexerPipeline::ensureSourceFile()
{
    if (!sourcePath_.empty()) {
        return sourcePath_;
    }
    if (sourceText_.empty()) {
        return {};
    }
    if (!ensureLangDir()) {
        return {};
    }
    const std::string p = langDir() + "/_input_src.txt";
    std::string err;
    if (!writeFile(p, sourceText_, err)) {
        return {};
    }
    return p;
}

bool LexerPipeline::writeFile(const std::string& path, const std::string& data, std::string& err)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        err = "无法打开文件 " + path;
        return false;
    }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out) {
        err = "写入失败 " + path;
        return false;
    }
    return true;
}

bool LexerPipeline::readFile(const std::string& path, std::string& out, std::string& err)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "无法打开文件 " + path;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    if (!in && !in.eof()) {
        err = "读取失败 " + path;
        return false;
    }
    out = ss.str();
    return true;
}

std::string LexerPipeline::trim(const std::string& s)
{
    const auto isSpace = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && isSpace(s[b])) ++b;
    while (e > b && isSpace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

} // namespace compiler::pipeline
