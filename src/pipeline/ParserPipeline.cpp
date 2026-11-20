#include "ParserPipeline.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>

#include "BnfParser.h"
#include "FirstFollow.h"
#include "GrammarSimplifier.h"
#include "IrGenerator.h"
#include "Lalr1Builder.h"
#include "Lr0Automaton.h"
#include "LrDriver.h"
#include "ParseTable.h"
#include "SampleLexReader.h"

namespace compiler::pipeline {

// ---- 输入 ----

void ParserPipeline::setBnfText(const std::string& text)
{
    const std::string t = stripBom(text);
    if (t == bnfText_) {
        return;   // 内容未变，不失效
    }
    bnfText_ = t;
    invalidateFrom(static_cast<int>(ParseStage::BnfParse));
}

void ParserPipeline::setBnfPath(const std::string& path)
{
    bnfPath_ = path;
}

void ParserPipeline::setSampleLexText(const std::string& text)
{
    const std::string t = stripBom(text);
    if (t == sampleLexText_) {
        return;
    }
    sampleLexText_ = t;
    // registry 可能变化（BnfParse 终结符→tokenCode 映射），全部失效
    invalidateFrom(static_cast<int>(ParseStage::LexRead));
}

void ParserPipeline::setSampleLexPath(const std::string& path)
{
    sampleLexPath_ = path;
}

bool ParserPipeline::setSampleLexFile(const std::string& path)
{
    std::string text;
    std::string err;
    if (!readFile(path, text, err)) {
        addDiag(Severity::Error, "sample-lex-read", "读取 sample.lex 文件失败：" + err);
        return false;
    }
    sampleLexPath_ = path;
    setSampleLexText(text);
    return true;
}

// ---- 回调 ----

void ParserPipeline::setStopCallback(std::function<bool()> cb)
{
    stopCb_ = std::move(cb);
}

void ParserPipeline::setStageCallback(std::function<void(ParseStage)> cb)
{
    stageCb_ = std::move(cb);
}

// ---- 主循环 ----

bool ParserPipeline::runThrough(ParseStage target)
{
    diags_.clear();
    if (static_cast<int>(target) < static_cast<int>(ParseStage::LexRead)) {
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
        const ParseStage st = static_cast<ParseStage>(s);
        if (stageCb_) {
            stageCb_(st);
        }

        bool ok = false;
        switch (st) {
        case ParseStage::LexRead:    ok = runStageLexRead(end);    break;
        case ParseStage::BnfParse:   ok = runStageBnfParse();      break;
        case ParseStage::Simplify:   ok = runStageSimplify();      break;
        case ParseStage::FirstFollow: ok = runStageFirstFollow();  break;
        case ParseStage::Lr0:        ok = runStageLr0();           break;
        case ParseStage::Lalr1:      ok = runStageLalr1();         break;
        case ParseStage::ParseTable: ok = runStageParseTable();    break;
        case ParseStage::Drive:      ok = runStageDrive();         break;
        case ParseStage::Ir:         ok = runStageIr();            break;
        default:                     ok = false;                   break;
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

void ParserPipeline::reset()
{
    invalidateFrom(static_cast<int>(ParseStage::LexRead));
    diags_.clear();
    lastRunOk_ = false;
}

// ---- 单阶段实现 ----

bool ParserPipeline::runStageLexRead(int targetIdx)
{
    if (sampleLexText_.empty()) {
        // 降级路径：target ≤ ParseTable 时跳过并继续；target ≥ Drive 时硬失败
        if (targetIdx >= static_cast<int>(ParseStage::Drive)) {
            addDiag(Severity::Error, "sample-lex-read",
                    "缺少 sample.lex：语法树/IR 阶段需要有效的 sample.lex（请先打开 sample.lex 文件）");
            return false;
        }
        addDiag(Severity::Info, "sample-lex-read",
                "未读取 sample.lex：终结符 tokenCode 置 -1（语法树/IR 阶段需要 sample.lex）");
        lexDataOk_ = false;
        return true;
    }

    lexer::SampleLexReader reader;
    auto res = reader.parse(sampleLexText_);
    for (auto& d : res.diagnostics) {
        diags_.push_back(std::move(d));
    }
    if (!res.ok) {
        return false;
    }
    registry_ = std::move(res.value.registry);
    tokens_ = std::move(res.value.tokens);
    sampleLexLang_ = std::move(res.value.languageName);
    lexDataOk_ = true;
    addDiag(Severity::Info, "sample-lex-read",
            "sample.lex 读取完成：" + std::to_string(tokens_.size()) + " 个 token");
    return true;
}

bool ParserPipeline::runStageBnfParse()
{
    if (bnfText_.empty()) {
        addDiag(Severity::Error, "bnf-parse", "未加载 .bnf 文法文件");
        return false;
    }
    grammar::BnfParser parser;
    // 主 A：LexRead 成功时用 sample.lex 头部重建的 registry 做终结符→tokenCode 映射；
    // 降级路径（无 sample.lex）传 nullptr → tokenCode=-1（T2.1 已支持）。
    auto res = parser.parse(bnfText_, lexDataOk_ ? &registry_ : nullptr);
    for (auto& d : res.diagnostics) {
        diags_.push_back(std::move(d));
    }
    if (!res.ok) {
        return false;
    }
    grammar_ = std::move(res.value);
    addDiag(Severity::Info, "bnf-parse",
            "BNF 解析完成：" + std::to_string(grammar_.symbols.size()) + " 个符号 / " +
            std::to_string(grammar_.productions.size()) + " 条产生式");
    return true;
}

bool ParserPipeline::runStageSimplify()
{
    grammar::GrammarSimplifier simplifier;
    auto res = simplifier.simplify(grammar_);
    for (auto& d : res.diagnostics) {
        diags_.push_back(std::move(d));
    }
    if (!res.ok) {
        return false;
    }
    simplifiedGrammar_ = std::move(res.value.grammar);
    simplifyLog_ = std::move(res.value.log);
    addDiag(Severity::Info, "simplify",
            "文法化简完成：" + std::to_string(simplifiedGrammar_.productions.size()) +
            " 条产生式" + (simplifyLog_.empty() ? "（文法已是最简）" : ""));
    return true;
}

bool ParserPipeline::runStageFirstFollow()
{
    grammar::FirstFollow firstFollow;
    auto res = firstFollow.compute(simplifiedGrammar_);
    for (auto& d : res.diagnostics) {
        diags_.push_back(std::move(d));
    }
    if (!res.ok) {
        return false;
    }
    firstFollow_ = std::move(res.value);
    addDiag(Severity::Info, "first-follow", "First/Follow 计算完成");
    return true;
}

bool ParserPipeline::runStageLr0()
{
    parser::Lr0Automaton lr0;
    auto res = lr0.build(simplifiedGrammar_);
    for (auto& d : res.diagnostics) {
        diags_.push_back(std::move(d));
    }
    if (!res.ok) {
        return false;
    }
    lr0_ = std::move(res.value);
    addDiag(Severity::Info, "lr0-build",
            "LR(0) 构建完成：" + std::to_string(lr0_.states.size()) + " 个状态");
    return true;
}

bool ParserPipeline::runStageLalr1()
{
    parser::Lalr1Builder builder;
    auto res = builder.build(simplifiedGrammar_, firstFollow_, lr0_);
    for (auto& d : res.diagnostics) {
        diags_.push_back(std::move(d));
    }
    if (!res.ok) {
        return false;
    }
    lalr1_ = std::move(res.value);
    addDiag(Severity::Info, "lalr1-build",
            "LALR(1) 构建完成：" + std::to_string(lalr1_.states.size()) + " 个状态");
    return true;
}

bool ParserPipeline::runStageParseTable()
{
    parser::ParseTableBuilder builder;
    auto res = builder.build(simplifiedGrammar_, lalr1_);
    for (auto& d : res.diagnostics) {
        diags_.push_back(std::move(d));
    }
    if (!res.ok) {
        return false;
    }
    parseTable_ = std::move(res.value);
    addDiag(Severity::Info, "parse-table",
            "分析表完成：" + std::to_string(parseTable_.conflicts.size()) + " 个冲突");
    return true;
}

bool ParserPipeline::runStageDrive()
{
    if (!lexDataOk_) {
        addDiag(Severity::Error, "parse", "缺少有效的 sample.lex token 流（LexRead 未成功）");
        return false;
    }
    parser::LrDriver driver;
    auto res = driver.parse(simplifiedGrammar_, parseTable_, tokens_);
    for (auto& d : res.diagnostics) {
        diags_.push_back(std::move(d));
    }
    if (!res.ok) {
        return false;
    }
    parseTree_ = std::make_shared<ParseTree>(std::move(res.value));
    addDiag(Severity::Info, "parse", "语法分析完成：已构建语法树");
    return true;
}

bool ParserPipeline::runStageIr()
{
    if (!parseTree_) {
        addDiag(Severity::Error, "ir", "缺少语法树（Drive 阶段未成功）");
        return false;
    }
    ir::IrGenerator generator;
    auto res = generator.generate(simplifiedGrammar_, *parseTree_);
    for (auto& d : res.diagnostics) {
        diags_.push_back(std::move(d));
    }
    if (!res.ok) {
        return false;
    }
    ir_ = std::move(res.value);
    addDiag(Severity::Info, "ir",
            "中间代码生成完成：" + std::to_string(ir_.quads.size()) + " 条四元式");
    return true;
}

// ---- 失效 ----

void ParserPipeline::invalidateFrom(int fromIdx)
{
    if (fromIdx <= static_cast<int>(ParseStage::LexRead)) {
        registry_ = TokenCodeRegistry{};
        tokens_.clear();
        lexDataOk_ = false;
        sampleLexLang_.clear();
    }
    if (fromIdx <= static_cast<int>(ParseStage::BnfParse)) {
        grammar_ = Grammar{};
    }
    if (fromIdx <= static_cast<int>(ParseStage::Simplify)) {
        simplifiedGrammar_ = Grammar{};
        simplifyLog_ = grammar::SimplifyLog{};
    }
    if (fromIdx <= static_cast<int>(ParseStage::FirstFollow)) {
        firstFollow_ = grammar::FirstFollowSets{};
    }
    if (fromIdx <= static_cast<int>(ParseStage::Lr0)) {
        lr0_ = parser::Lr0AutomatonData{};
    }
    if (fromIdx <= static_cast<int>(ParseStage::Lalr1)) {
        lalr1_ = parser::Lalr1Data{};
    }
    if (fromIdx <= static_cast<int>(ParseStage::ParseTable)) {
        parseTable_ = parser::ParseTableData{};
    }
    if (fromIdx <= static_cast<int>(ParseStage::Drive)) {
        parseTree_.reset();
    }
    if (fromIdx <= static_cast<int>(ParseStage::Ir)) {
        ir_ = ir::IrProgram{};
    }
    const int prev = fromIdx - 1;
    if (static_cast<int>(currentStage_) > prev) {
        currentStage_ = (prev >= static_cast<int>(ParseStage::LexRead))
                            ? static_cast<ParseStage>(prev)
                            : ParseStage::None;
    }
}

// ---- 查询 ----

ParserPipelineSnapshot ParserPipeline::snapshot() const
{
    ParserPipelineSnapshot snap;
    snap.ok = lastRunOk_;
    snap.validThrough = currentStage_;
    snap.grammar = grammar_;
    snap.simplifiedGrammar = simplifiedGrammar_;
    snap.simplifyLog = simplifyLog_;
    snap.firstFollow = firstFollow_;
    snap.lr0 = lr0_;
    snap.lalr1 = lalr1_;
    snap.parseTable = parseTable_;
    snap.registry = registry_;
    snap.tokens = tokens_;
    snap.parseTree = parseTree_;
    snap.ir = ir_;
    snap.languageName = languageName();
    snap.diagnostics = diags_;
    return snap;
}

std::string ParserPipeline::languageName() const
{
    if (!sampleLexLang_.empty()) {
        return sampleLexLang_;
    }
    const std::string base = fileStem(bnfPath_);
    return base.empty() ? std::string("lang") : base;
}

// ---- 工具 ----

void ParserPipeline::addDiag(Severity sev, const std::string& stage,
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

bool ParserPipeline::stopRequested() const
{
    return stopCb_ && stopCb_();
}

std::string ParserPipeline::stripBom(const std::string& s)
{
    if (s.size() >= 3 &&
        static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB &&
        static_cast<unsigned char>(s[2]) == 0xBF) {
        return s.substr(3);
    }
    return s;
}

bool ParserPipeline::readFile(const std::string& path, std::string& out, std::string& err)
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

std::string ParserPipeline::fileStem(const std::string& path)
{
    const std::size_t slash = path.find_last_of("/\\");
    std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
    const std::size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) {
        base = base.substr(0, dot);
    }
    return base;
}

// ---- 格式化工具（CLI --parse / 单测 / 诊断复用） ----

std::string formatParseTree(const ParseTree& tree)
{
    std::string out;
    if (!tree.root) {
        return out;
    }
    std::function<void(const ParseTreeNode*, int)> visit =
        [&out, &visit](const ParseTreeNode* node, int depth) {
            out.append(static_cast<size_t>(depth) * 2, ' ');
            if (node->isLeaf()) {
                out += node->text.empty() ? "(ε)" : node->text;
            } else {
                out += node->text;
                if (node->productionId >= 0) {
                    out += " [prod " + std::to_string(node->productionId) + "]";
                }
            }
            out += "\n";
            for (const auto& child : node->children) {
                visit(child.get(), depth + 1);
            }
        };
    visit(tree.root.get(), 0);
    return out;
}

std::string formatIrProgram(const ir::IrProgram& ir)
{
    std::string out;
    for (const ir::Quadruple& q : ir.quads) {
        out += "(" + q.op + ", " + q.arg1 + ", " + q.arg2 + ", " + q.result + ")\n";
    }
    return out;
}

} // namespace compiler::pipeline
