#include "JointPipeline.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>

namespace compiler::pipeline {
namespace {

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

// ---- 阶段名 ----

const char* jointStageName(JointStage s)
{
    switch (s) {
    case JointStage::None:   return "None";
    case JointStage::LexRun: return "LexRun";
    case JointStage::Parse:  return "Parse";
    case JointStage::Ir:     return "Ir";
    }
    return "?";
}

const char* lexStageName(Stage s)
{
    switch (s) {
    case Stage::None:     return "None";
    case Stage::Rules:    return "Rules";
    case Stage::Nfa:      return "Nfa";
    case Stage::Dfa:      return "Dfa";
    case Stage::Minimize: return "Minimize";
    case Stage::Codegen:  return "Codegen";
    case Stage::Compile:  return "Compile";
    case Stage::Run:      return "Run";
    }
    return "?";
}

const char* parseStageName(ParseStage s)
{
    switch (s) {
    case ParseStage::None:        return "None";
    case ParseStage::LexRead:     return "LexRead";
    case ParseStage::BnfParse:    return "BnfParse";
    case ParseStage::Simplify:    return "Simplify";
    case ParseStage::FirstFollow: return "FirstFollow";
    case ParseStage::Lr0:         return "Lr0";
    case ParseStage::Lalr1:       return "Lalr1";
    case ParseStage::ParseTable:  return "ParseTable";
    case ParseStage::Drive:       return "Drive";
    case ParseStage::Ir:          return "Ir";
    }
    return "?";
}

// ---- 构造 ----

JointPipeline::JointPipeline(CompileRunBackend& backend)
    : lex_(backend)
{
}

// ---- 输入 ----

void JointPipeline::setRulesText(const std::string& text)
{
    const Stage before = lex_.currentStage();
    lex_.setRulesText(text);
    if (lex_.currentStage() < before) {
        invalidateFrom(JointStage::LexRun);
    } else if (presetLexUsed_ && text != rulesTextCache_) {
        // 规则已换：预置产物与旧语言绑定，必须作废
        invalidateFrom(JointStage::LexRun);
    }
    rulesTextCache_ = text;
}

void JointPipeline::setRulesPath(const std::string& path)
{
    lex_.setRulesPath(path);   // 仅影响语言名与展示，不失效
}

void JointPipeline::setSourceText(const std::string& text)
{
    const Stage before = lex_.currentStage();
    lex_.setSourceText(text);
    const std::string now = lex_.sourceText();   // 已去 BOM，比较以它为准
    if (lex_.currentStage() < before) {
        invalidateFrom(JointStage::LexRun);
    } else if (presetLexUsed_ && now != sourceTextCache_) {
        // 源程序已改：预置产物与之不再对应，必须作废（避免拿旧 token 流装新程序）
        invalidateFrom(JointStage::LexRun);
    }
    sourceTextCache_ = now;
}

bool JointPipeline::setSourcePath(const std::string& path)
{
    const Stage before = lex_.currentStage();
    if (!lex_.setSourcePath(path)) {
        // 词法侧已记 stage="input" 的 Error（该方法不经过 runThrough），并入累积视图
        collectLexDiagnostics();
        rebuildDiagnostics();
        return false;
    }
    sourcePath_ = path;
    if (lex_.currentStage() < before) {
        invalidateFrom(JointStage::LexRun);
    }
    return true;
}

void JointPipeline::setOutputDir(const std::string& dir)
{
    const Stage before = lex_.currentStage();
    lex_.setOutputDir(dir);
    if (presetLexUsed_ && dir != outputDir_) {
        invalidateFrom(JointStage::LexRun);   // 输出目录已换：预置产物路径失效
    }
    outputDir_ = dir;
    if (lex_.currentStage() < before) {
        invalidateFrom(JointStage::LexRun);
    }
}

void JointPipeline::setBnfText(const std::string& text)
{
    const ParseStage before = parser_.currentStage();
    parser_.setBnfText(text);
    bnfTextCache_ = text;
    if (parser_.currentStage() < before) {
        invalidateFrom(JointStage::Parse);   // 词法产物保留
    }
}

void JointPipeline::setBnfPath(const std::string& path)
{
    parser_.setBnfPath(path);
    bnfPathCache_ = path;
}

bool JointPipeline::loadInputs(const std::string& rulesPath,
                               const std::string& bnfPath,
                               const std::string& sourcePath)
{
    jointDiags_.clear();
    std::string rules;
    std::string bnf;
    std::string err;
    if (!readFile(rulesPath, rules, err)) {
        addDiag(Severity::Error, "input", "读取 .rules 失败：" + err);
        rebuildDiagnostics();
        return false;
    }
    if (!readFile(bnfPath, bnf, err)) {
        addDiag(Severity::Error, "input", "读取 .bnf 失败：" + err);
        rebuildDiagnostics();
        return false;
    }
    setRulesPath(rulesPath);
    setRulesText(rules);
    setBnfPath(bnfPath);
    setBnfText(bnf);
    if (!setSourcePath(sourcePath)) {
        rebuildDiagnostics();
        return false;
    }
    rebuildDiagnostics();
    return true;
}

// ---- 回调 ----

void JointPipeline::setStopCallback(std::function<bool()> cb)
{
    stopCb_ = cb;
    lex_.setStopCallback(cb);
    parser_.setStopCallback(std::move(cb));
}

void JointPipeline::setStageCallback(std::function<void(JointStage)> cb)
{
    stageCb_ = std::move(cb);
}

void JointPipeline::setLexStageCallback(std::function<void(Stage)> cb)
{
    lex_.setStageCallback(std::move(cb));
}

void JointPipeline::setParseStageCallback(std::function<void(ParseStage)> cb)
{
    parser_.setStageCallback(std::move(cb));
}

// ---- 主循环 ----

bool JointPipeline::runThrough(JointStage target)
{
    jointDiags_.clear();
    if (currentStage_ == JointStage::None) {
        // 新一条联合链路：从头累积诊断（避免上一轮残留）
        lexDiags_.clear();
        parseDiags_.clear();
    }
    if (static_cast<int>(target) < static_cast<int>(JointStage::LexRun)) {
        lastRunOk_ = true;
        rebuildDiagnostics();
        return true;   // 无事可做（GUI 不会传 None）
    }
    if (stopRequested()) {
        addDiag(Severity::Info, "joint", "已取消");
        lastRunOk_ = false;
        rebuildDiagnostics();
        return false;
    }

    const int end = static_cast<int>(target);
    for (int s = static_cast<int>(currentStage_) + 1; s <= end; ++s) {
        if (stopRequested()) {
            addDiag(Severity::Info, "joint", "已取消");
            lastRunOk_ = false;
            rebuildDiagnostics();
            return false;
        }
        const JointStage st = static_cast<JointStage>(s);
        if (stageCb_) {
            stageCb_(st);
        }

        bool ok = false;
        switch (st) {
        case JointStage::LexRun: ok = runStageLexRun(); break;
        case JointStage::Parse:  ok = runStageParse();  break;
        case JointStage::Ir:     ok = runStageIr();     break;
        default:                 ok = false;            break;
        }
        if (!ok) {
            lastRunOk_ = false;
            rebuildDiagnostics();
            return false;
        }
        currentStage_ = st;
    }
    lastRunOk_ = true;
    rebuildDiagnostics();
    return true;
}

bool JointPipeline::runStageLexRun()
{
    if (presetLexUsed_) {
        // 预置通道：词法产物已由 loadPresetLex 注入，跳过 Codegen/Compile/Run
        addDiag(Severity::Info, "joint-preset",
                "使用预置 sample.lex（" + presetLexPath_
                    + "），跳过生成扫描器的编译与运行");
        return true;
    }
    return runLexerThrough(Stage::Run);
}

bool JointPipeline::runStageParse()
{
    return runParserThrough(ParseStage::Drive);
}

bool JointPipeline::runStageIr()
{
    return runParserThrough(ParseStage::Ir);
}

bool JointPipeline::runLexerThrough(Stage target)
{
    jointDiags_.clear();
    if (static_cast<int>(target) < static_cast<int>(Stage::Rules)) {
        lastRunOk_ = true;
        rebuildDiagnostics();
        return true;
    }

    const bool lexOk = lex_.runThrough(target);
    collectLexDiagnostics();
    const PipelineSnapshot ls = lex_.snapshot();

    // 只有推进到 Run 才需要严格校验并注入语法流水线
    if (target != Stage::Run) {
        lastRunOk_ = lexOk;
        rebuildDiagnostics();
        return lexOk;
    }

    // ---- 绝对强制校验：必须是生成扫描器真实产出的 sample.lex ----
    if (!verifyLexProduced(ls)) {
        strictLexOk_ = false;
        sampleLexPath_.clear();
        lastRunOk_ = false;
        rebuildDiagnostics();
        return false;
    }
    // 双保险：runThrough 返回值也必须成功
    if (!lexOk) {
        addDiag(Severity::Error, "joint-lex",
                "词法流水线返回失败但产物标记为真实运行，联合流水线终止（状态不一致）");
        strictLexOk_ = false;
        sampleLexPath_.clear();
        lastRunOk_ = false;
        rebuildDiagnostics();
        return false;
    }

    // 真实产物就绪：记录路径并注入语法流水线
    sampleLexPath_ = sampleLexPathFor(ls.languageName);
    strictLexOk_ = true;
    feedParser(ls.runLexText);
    if (currentStage_ < JointStage::LexRun) {
        currentStage_ = JointStage::LexRun;
    }
    lastRunOk_ = true;
    rebuildDiagnostics();
    return true;
}

bool JointPipeline::runParserThrough(ParseStage target)
{
    jointDiags_.clear();
    if (!strictLexOk_) {
        addDiag(Severity::Error, "joint-parse",
                "语法阶段前置未满足：必须先用生成扫描器真实产出 sample.lex"
                "（runProducedLex == true），联合流水线拒绝把进程内预览送入语法分析");
        lastRunOk_ = false;
        rebuildDiagnostics();
        return false;
    }

    const bool ok = parser_.runThrough(target);
    collectParseDiagnostics();
    if (ok) {
        if (static_cast<int>(target) >= static_cast<int>(ParseStage::Ir)) {
            currentStage_ = JointStage::Ir;
        } else if (static_cast<int>(target) >= static_cast<int>(ParseStage::Drive)
                   && currentStage_ < JointStage::Parse) {
            currentStage_ = JointStage::Parse;
        }
    }
    lastRunOk_ = ok;
    rebuildDiagnostics();
    return ok;
}

// ---- 预置产物通道 ----

bool JointPipeline::loadPresetLex(const std::string& lexPath)
{
    jointDiags_.clear();

    std::string text;
    std::string err;
    if (!readFile(lexPath, text, err)) {
        addDiag(Severity::Error, "joint-preset", "载入预置 sample.lex 失败：" + err);
        lastRunOk_ = false;
        rebuildDiagnostics();
        return false;
    }
    if (text.empty()) {
        addDiag(Severity::Error, "joint-preset", "预置 sample.lex 内容为空：" + lexPath);
        lastRunOk_ = false;
        rebuildDiagnostics();
        return false;
    }

    presetLexUsed_ = true;
    presetLexText_ = std::move(text);
    presetLexPath_ = lexPath;
    sampleLexPath_ = lexPath;
    // 记录绑定时的输入：源程序或规则一变，预置产物即作废
    sourceTextCache_ = lex_.sourceText();
    // 显式载入即视为语法阶段前置成立：产物是任务一真实运行的结果，
    // 只是产生于另一台机器。进程内预览不会走到这里（它没有路径、不落盘）。
    strictLexOk_ = true;
    if (currentStage_ < JointStage::LexRun) {
        currentStage_ = JointStage::LexRun;
    }
    feedParser(presetLexText_);

    addDiag(Severity::Warning, "joint-preset",
            "已载入预置 sample.lex（本机未运行生成扫描器；内容为任务一真实运行产物）："
                + lexPath);
    lastRunOk_ = true;
    rebuildDiagnostics();
    return true;
}

bool JointPipeline::loadPresetLexFromOutputDir()
{
    const std::string direct = sampleLexPathFor(lex_.snapshot().languageName);
    {
        std::ifstream probe(direct, std::ios::binary);
        if (probe.good()) {
            return loadPresetLex(direct);
        }
    }

    // 兜底：<outputDir>/<@name>/sample.lex 与目录名大小写不一致时，
    // 扫描 <outputDir>/*/sample.lex 取第一个可读文件（发布包只有一份语言）
    std::error_code ec;
    for (std::filesystem::directory_iterator it(outputDir_, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec) || ec) {
            continue;
        }
        const std::filesystem::path candidate = it->path() / "sample.lex";
        std::ifstream probe(candidate, std::ios::binary);
        if (probe.good()) {
            return loadPresetLex(candidate.string());
        }
    }

    addDiag(Severity::Error, "joint-preset",
            "未找到预置 sample.lex：" + direct
                + "（请确认发布包 generated/ 目录完整，或在任务一配好 g++ 后重新生成）");
    lastRunOk_ = false;
    rebuildDiagnostics();
    return false;
}

// ---- 严格校验 ----

bool JointPipeline::verifyLexProduced(const PipelineSnapshot& lexSnap)
{
    if (lexSnap.runProducedLex) {
        if (static_cast<int>(lexSnap.validThrough) < static_cast<int>(Stage::Run)) {
            addDiag(Severity::Error, "joint-strict",
                    std::string("词法状态不一致：runProducedLex=true 但只推进到 ")
                    + lexStageName(lexSnap.validThrough) + " 阶段");
            return false;
        }
        return true;
    }

    // 区分「编译/运行失败后回退到进程内预览」与「上游阶段就失败」
    const bool fellBack = !lexSnap.previewLexText.empty()
        || static_cast<int>(lexSnap.validThrough) >= static_cast<int>(Stage::Compile);
    if (fellBack) {
        addDiag(Severity::Error, "joint-strict",
                std::string("词法阶段未产出真实 sample.lex（runProducedLex=false）：")
                + "生成扫描器在 " + lexStageName(lexSnap.validThrough)
                + " 阶段失败后回退到进程内预览；联合流水线拒绝把预览结果送入语法分析"
                  "（预览仅供失败诊断与界面提示）");
        return false;
    }
    addDiag(Severity::Error, "joint-lex",
            std::string("词法流水线未推进到 Run（停在 ") + lexStageName(lexSnap.validThrough)
            + " 阶段），无法产出 sample.lex");
    return false;
}

// ---- 内部工具 ----

void JointPipeline::feedParser(const std::string& runLexText)
{
    // BNF 输入重放（ParserPipeline 内部对相同文本不失效）
    if (!bnfPathCache_.empty()) {
        parser_.setBnfPath(bnfPathCache_);
    }
    if (!bnfTextCache_.empty()) {
        parser_.setBnfText(bnfTextCache_);
    }
    parser_.setSampleLexPath(sampleLexPath_);
    parser_.setSampleLexText(runLexText);
}

void JointPipeline::invalidateFrom(JointStage from)
{
    if (from == JointStage::None) {
        return;
    }
    if (static_cast<int>(from) > static_cast<int>(currentStage_)) {
        return;   // 该阶段之后本来就无有效产物
    }
    if (from == JointStage::LexRun) {
        strictLexOk_ = false;
        sampleLexPath_.clear();
        // 输入已变：预置产物随之作废，必须重新载入或真实运行
        presetLexUsed_ = false;
        presetLexText_.clear();
        presetLexPath_.clear();
        // 词法输入变更：语法产物整体作废（reset 清空 LexRead..Ir 与诊断），
        // 否则上一轮的语法树/IR 会被当成新输入的结果。
        parser_.reset();
        lexDiags_.clear();
        parseDiags_.clear();
    } else if (from == JointStage::Parse) {
        parseDiags_.clear();   // BNF 变更：仅语法侧诊断作废
    }
    currentStage_ = static_cast<JointStage>(static_cast<int>(from) - 1);
    lastRunOk_ = false;
}

void JointPipeline::collectLexDiagnostics()
{
    for (const Diagnostic& d : lex_.diagnostics()) {
        lexDiags_.push_back(d);
    }
}

void JointPipeline::collectParseDiagnostics()
{
    for (const Diagnostic& d : parser_.diagnostics()) {
        parseDiags_.push_back(d);
    }
}

void JointPipeline::rebuildDiagnostics()
{
    diags_.clear();
    diags_.reserve(lexDiags_.size() + parseDiags_.size() + jointDiags_.size());
    for (const Diagnostic& d : lexDiags_) {
        diags_.push_back(d);
    }
    for (const Diagnostic& d : parseDiags_) {
        diags_.push_back(d);
    }
    for (const Diagnostic& d : jointDiags_) {
        diags_.push_back(d);
    }
}

void JointPipeline::addDiag(Severity sev, const std::string& stage,
                            const std::string& msg, int line)
{
    Diagnostic d;
    d.severity = sev;
    d.stage = stage;
    d.message = msg;
    d.loc.line = line;
    d.loc.column = -1;
    d.loc.offset = -1;
    jointDiags_.push_back(std::move(d));
}

bool JointPipeline::stopRequested() const
{
    return stopCb_ && stopCb_();
}

std::string JointPipeline::sampleLexPathFor(const std::string& languageName) const
{
    std::string dir = outputDir_;
    while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\')) {
        dir.pop_back();
    }
    const std::string lang = languageName.empty() ? std::string("lang") : languageName;
    return dir + "/" + lang + "/sample.lex";
}

bool JointPipeline::readFile(const std::string& path, std::string& out, std::string& err)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "无法打开文件 " + path;
        return false;
    }
    std::string data((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    if (!in.good() && !in.eof()) {
        err = "读取文件失败 " + path;
        return false;
    }
    out = stripBom(data);
    return true;
}

JointSnapshot JointPipeline::snapshot() const
{
    JointSnapshot snap;
    snap.ok = lastRunOk_;
    snap.validThrough = currentStage_;
    snap.lex = lex_.snapshot();
    snap.parse = parser_.snapshot();
    snap.lexStage = lex_.currentStage();
    snap.parseStage = parser_.currentStage();
    snap.runProducedLex = snap.lex.runProducedLex;
    snap.strictLexOk = strictLexOk_;
    snap.presetLex = presetLexUsed_;
    snap.presetLexPath = presetLexPath_;
    if (presetLexUsed_) {
        // 词法流水线未运行：把预置文本放进 runLexText，界面与报告据此展示与统计
        snap.lex.runLexText = presetLexText_;
    }
    snap.sampleLexPath = presetLexUsed_ ? presetLexPath_ : sampleLexPath_;
    snap.sourcePath = sourcePath_;
    snap.languageName = snap.lex.languageName;
    snap.diagnostics = diags_;
    return snap;
}

} // namespace compiler::pipeline
