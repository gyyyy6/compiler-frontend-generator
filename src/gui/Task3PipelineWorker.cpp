#include "Task3PipelineWorker.h"

#include <cstdio>
#include <ctime>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <QElapsedTimer>
#include <QString>

#include "Grammar.h"
#include "ParseTable.h"
#include "ParseTree.h"
#include "ParserPipeline.h"
#include "SampleLexReader.h"
#include "Token.h"

Q_DECLARE_METATYPE(compiler::pipeline::JointSnapshot)

namespace compiler::gui {
namespace {

using pipeline::JointSnapshot;
using pipeline::JointStage;
using pipeline::ParseStage;
using pipeline::Stage;

// ---- 阶段名（中文，GUI/报告复用；与 pipeline::*StageName 的英文名互补）----

const char* lexStageLabel(Stage s)
{
    switch (s) {
    case Stage::None:     return "未开始";
    case Stage::Rules:    return "解析 .rules";
    case Stage::Nfa:      return "构建 NFA";
    case Stage::Dfa:      return "子集构造 DFA";
    case Stage::Minimize: return "最小化 DFA";
    case Stage::Codegen:  return "生成扫描器源码";
    case Stage::Compile:  return "编译扫描器";
    case Stage::Run:      return "运行扫描器产出 sample.lex";
    }
    return "未知";
}

const char* parseStageLabel(ParseStage s)
{
    switch (s) {
    case ParseStage::None:        return "未开始";
    case ParseStage::LexRead:     return "读取 sample.lex";
    case ParseStage::BnfParse:    return "解析 BNF 文法";
    case ParseStage::Simplify:    return "文法化简";
    case ParseStage::FirstFollow: return "First/Follow";
    case ParseStage::Lr0:         return "LR(0) 项目集";
    case ParseStage::Lalr1:       return "LALR(1) 合并";
    case ParseStage::ParseTable:  return "生成分析表";
    case ParseStage::Drive:       return "LR 驱动 / 语法树";
    case ParseStage::Ir:          return "生成四元式 IR";
    }
    return "未知";
}

const char* jointStageLabel(JointStage s)
{
    switch (s) {
    case JointStage::None:   return "未开始";
    case JointStage::LexRun: return "词法全程（生成扫描器真实运行）";
    case JointStage::Parse:  return "语法分析（LALR(1) + 语法树）";
    case JointStage::Ir:     return "中间代码（四元式 IR）";
    }
    return "未知";
}

const char* kindText(TokenKind k)
{
    switch (k) {
    case TokenKind::EndOfFile:  return "EOF";
    case TokenKind::Keyword:    return "Keyword";
    case TokenKind::Identifier: return "Identifier";
    case TokenKind::Number:     return "Number";
    case TokenKind::String:     return "String";
    case TokenKind::Operator:   return "Operator";
    case TokenKind::Punct:      return "Punct";
    case TokenKind::Comment:    return "Comment";
    case TokenKind::Error:      return "Error";
    case TokenKind::Unknown:    return "Unknown";
    }
    return "Unknown";
}

const char* severityText(Severity s)
{
    switch (s) {
    case Severity::Info:    return "Info";
    case Severity::Warning: return "Warning";
    case Severity::Error:   return "Error";
    }
    return "Info";
}

bool startsWith(const std::string& s, const char* prefix)
{
    const std::string p(prefix);
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

// 单行化（Markdown 表格单元格内不能有换行）
std::string oneLine(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\n' || c == '\r' || c == '\t') {
            out.push_back(' ');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

// 表格单元格转义（'|' 需转义；先单行化）
std::string cell(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : oneLine(s)) {
        if (c == '|') {
            out += "\\|";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string symbolNameOf(const Grammar& g, int id)
{
    const Symbol* s = g.symbol(id);
    if (s) {
        return s->name;
    }
    return std::string("?") + std::to_string(id);
}

std::string productionText(const Grammar& g, const Production& p)
{
    std::string rhs;
    for (int sid : p.rhs) {
        if (!rhs.empty()) {
            rhs += " ";
        }
        rhs += symbolNameOf(g, sid);
    }
    if (rhs.empty()) {
        rhs = "ε";
    }
    return symbolNameOf(g, p.lhs) + " -> " + rhs;
}

std::string nowText()
{
    const std::time_t t = std::time(nullptr);
    std::tm tmv{};
#if defined(_MSC_VER)
    localtime_s(&tmv, &t);
#else
    const std::tm* p = std::localtime(&t);
    if (p) {
        tmv = *p;
    }
#endif
    char buf[32] = {0};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
    return buf;
}

std::string percentText(int covered, int total)
{
    if (total <= 0) {
        return "—";
    }
    char buf[32] = {0};
    std::snprintf(buf, sizeof(buf), "%.1f", 100.0 * covered / total);
    return buf;
}

// ---- 16 阶段时间线 ----

struct StageRow {
    const char* group;
    const char* name;
};

const StageRow kStageRows[16] = {
    {"词法", "解析 .rules"},
    {"词法", "构建 NFA"},
    {"词法", "子集构造 DFA"},
    {"词法", "最小化 DFA"},
    {"词法", "生成扫描器源码"},
    {"词法", "编译扫描器"},
    {"词法", "运行扫描器产出 sample.lex"},
    {"语法", "读取 sample.lex"},
    {"语法", "解析 BNF 文法"},
    {"语法", "文法化简"},
    {"语法", "First/Follow"},
    {"语法", "LR(0) 项目集"},
    {"语法", "LALR(1) 合并"},
    {"语法", "生成分析表"},
    {"语法", "LR 驱动 / 语法树"},
    {"语法", "生成四元式 IR"},
};

// 诊断 stage 字符串 → 时间线下标（-1 = 无法定位）
int stageIndexForDiag(const std::string& stage)
{
    static const struct {
        const char* stage;
        int index;
    } kMap[] = {
        {"rules-parse", 0},   {"nfa-build", 1},   {"dfa-build", 2},
        {"dfa-minimize", 3},  {"codegen", 4},     {"compile", 5},
        {"run", 6},           {"joint-lex", 6},    {"joint-strict", 6},
        {"sample-lex-read", 7}, {"bnf-parse", 8},  {"simplify", 9},
        {"first-follow", 10}, {"lr0-build", 11},   {"lalr1-build", 12},
        {"parse-table", 13},  {"parse", 14},       {"joint-parse", 14},
        {"ir", 15},
    };
    for (const auto& m : kMap) {
        if (stage == m.stage) {
            return m.index;
        }
    }
    return -1;
}

// 首个可定位的 Error 诊断所在阶段（跳过 input 等无法定位阶段的错误）
int failureIndex(const JointSnapshot& snap)
{
    for (const Diagnostic& d : snap.diagnostics) {
        if (d.severity != Severity::Error) {
            continue;
        }
        const int idx = stageIndexForDiag(d.stage);
        if (idx >= 0) {
            return idx;
        }
    }
    return -1;
}

// 该阶段的首条 Error 消息（用于时间线「说明」列）
std::string failureMessageAt(const JointSnapshot& snap, int index)
{
    for (const Diagnostic& d : snap.diagnostics) {
        if (d.severity == Severity::Error && stageIndexForDiag(d.stage) == index) {
            return d.message;
        }
    }
    return {};
}

bool stageSucceeded(int index, int lexValid, int parseValid)
{
    if (index < 7) {
        return index <= lexValid;
    }
    return (index - 7) <= parseValid;
}

struct TreeStats {
    int nodes = 0;
    int leaves = 0;
    int depth = 0;
};

void walkTree(const ParseTreeNode* node, int depth, TreeStats& stats)
{
    if (!node) {
        return;
    }
    ++stats.nodes;
    if (node->children.empty()) {
        ++stats.leaves;
    }
    if (depth > stats.depth) {
        stats.depth = depth;
    }
    for (const auto& child : node->children) {
        walkTree(child.get(), depth + 1, stats);
    }
}

void collectProductionIds(const ParseTreeNode* node, std::set<int>& out)
{
    if (!node) {
        return;
    }
    if (node->productionId >= 0) {
        out.insert(node->productionId);
    }
    for (const auto& child : node->children) {
        collectProductionIds(child.get(), out);
    }
}

} // namespace

// ---- 覆盖矩阵 ----

std::vector<Token> jointTokens(const JointSnapshot& snapshot)
{
    if (!snapshot.parse.tokens.empty()) {
        return snapshot.parse.tokens;   // LexRead 产物（与语法分析同一份）
    }
    if (snapshot.lex.runLexText.empty()) {
        return {};
    }
    lexer::SampleLexReader reader;
    auto res = reader.parse(snapshot.lex.runLexText);
    if (!res.ok) {
        return {};
    }
    return std::move(res.value.tokens);
}

JointCoverage computeJointCoverage(const JointSnapshot& snapshot)
{
    JointCoverage cov;
    const Grammar& g = snapshot.parse.simplifiedGrammar;
    if (g.productions.empty()) {
        return cov;   // 语法阶段未推进：无文法可对照
    }

    std::set<std::string> tokenNames;
    for (const Token& t : jointTokens(snapshot)) {
        tokenNames.insert(t.name);
    }

    for (const Symbol& s : g.symbols) {
        if (s.kind != SymbolKind::Terminal) {
            continue;
        }
        if (startsWith(s.name, "KW_")) {
            cov.keywords.push_back(CoverageItem{s.name, tokenNames.count(s.name) != 0, false});
        } else if (startsWith(s.name, "SYM_") || startsWith(s.name, "OP_")) {
            cov.symbols.push_back(CoverageItem{s.name, tokenNames.count(s.name) != 0, false});
        }
    }

    std::set<int> used;
    if (snapshot.parse.parseTree) {
        collectProductionIds(snapshot.parse.parseTree->root.get(), used);
    }
    for (const Production& p : g.productions) {
        CoverageItem item;
        item.label = std::to_string(p.id) + ": " + productionText(g, p);
        item.covered = used.count(p.id) != 0;
        item.epsilon = p.rhs.empty();   // ε 归约不产生树节点
        cov.productions.push_back(std::move(item));
    }
    return cov;
}

std::string coverageSummary(const std::vector<CoverageItem>& items)
{
    int total = 0;
    int covered = 0;
    int epsilons = 0;
    for (const CoverageItem& it : items) {
        if (it.epsilon) {
            ++epsilons;
            continue;
        }
        ++total;
        if (it.covered) {
            ++covered;
        }
    }
    if (total == 0) {
        if (epsilons > 0) {
            return "ε 产生式 " + std::to_string(epsilons) + " 条（无树节点）";
        }
        return "—";
    }
    std::string out = std::to_string(covered) + "/" + std::to_string(total) + "（"
        + percentText(covered, total) + "%）";
    if (epsilons > 0) {
        out += "，另有 ε 产生式 " + std::to_string(epsilons) + " 条（无树节点，不计入）";
    }
    return out;
}

// ---- 阶段时间线 ----

std::string formatJointTimeline(const JointSnapshot& snapshot)
{
    const int lexValid = static_cast<int>(snapshot.lex.validThrough);
    const int parseValid = static_cast<int>(snapshot.parse.validThrough);
    const int failIdx = failureIndex(snapshot);

    std::ostringstream out;
    out << "| # | 阶段组 | 子阶段 | 状态 | 说明 |\n";
    out << "| --- | --- | --- | --- | --- |\n";
    for (int i = 0; i < 16; ++i) {
        const bool ok = stageSucceeded(i, lexValid, parseValid);
        const char* status = ok ? "✓ 成功" : (i == failIdx ? "✗ 失败" : "— 未执行");
        std::string note;
        if (!ok && i == failIdx) {
            note = cell(failureMessageAt(snapshot, i));
        }
        out << "| " << (i + 1) << " | " << kStageRows[i].group << " | "
            << kStageRows[i].name << " | " << status << " | " << note << " |\n";
    }
    return out.str();
}

// ---- Markdown 报告 ----

std::string formatJointReportMarkdown(const JointSnapshot& snapshot,
                                      const JointReportContext& context)
{
    const std::vector<Token> tokens = jointTokens(snapshot);
    int errorTokens = 0;
    for (const Token& t : tokens) {
        if (t.isError()) {
            ++errorTokens;
        }
    }

    const Grammar& g = snapshot.parse.simplifiedGrammar;
    const ParseTree* tree = snapshot.parse.parseTree.get();
    TreeStats stats;
    if (tree) {
        walkTree(tree->root.get(), 0, stats);
    }
    const int quadCount = static_cast<int>(snapshot.parse.ir.quads.size());
    const int conflictCount = static_cast<int>(snapshot.parse.parseTable.conflicts.size());
    const JointCoverage coverage = computeJointCoverage(snapshot);

    std::ostringstream out;
    out << "# Mini-C 联合测试报告\n\n";
    out << "- 生成时间：" << nowText() << "\n";
    out << "- 语言：" << (snapshot.languageName.empty() ? "（未知）" : snapshot.languageName) << "\n";
    if (!context.rulesPath.empty()) {
        out << "- 规则文件：" << context.rulesPath << "\n";
    }
    if (!context.bnfPath.empty()) {
        out << "- 文法文件：" << context.bnfPath << "\n";
    }
    out << "- 源程序：" << (context.sourcePath.empty() ? "（未指定）" : context.sourcePath) << "\n";
    if (!context.outputDir.empty()) {
        out << "- 输出目录：" << context.outputDir << "\n";
    }
    out << "- sample.lex："
        << (snapshot.sampleLexPath.empty() ? "（未产出）" : snapshot.sampleLexPath)
        << "（" << (snapshot.runProducedLex ? "真实生成扫描器产出" : "非真实产出 / 未产出") << "）\n";
    out << "- 联合阶段：" << pipeline::jointStageName(snapshot.validThrough) << "（"
        << (snapshot.ok ? "成功" : "失败") << "）\n";
    out << "- 严格校验（runProducedLex）："
        << (snapshot.strictLexOk ? "通过" : "未通过") << "\n\n";

    // ---- 1. 结论 ----
    out << "## 1. 结论\n\n";
    out << "- 结果：**" << (snapshot.ok ? "成功" : "失败") << "**\n";
    out << "- 词法：";
    if (snapshot.runProducedLex) {
        out << "真实生成扫描器产出 sample.lex，" << tokens.size() << " tokens，"
            << errorTokens << " ERROR\n";
    } else {
        out << "未获得真实 sample.lex（严格校验拒绝进入语法分析）\n";
    }
    if (tree) {
        out << "- 语法：化简后 " << g.productions.size() << " 条产生式，LR(0) "
            << snapshot.parse.lr0.states.size() << " 态，LALR(1) "
            << snapshot.parse.lalr1.states.size() << " 态，" << conflictCount
            << " 个冲突；语法树 " << stats.nodes << " 节点（叶 " << stats.leaves
            << "，深度 " << stats.depth << "）\n";
    } else {
        out << "- 语法：未生成语法树\n";
    }
    out << "- IR：" << (quadCount > 0 ? std::to_string(quadCount) + " 条四元式" : "未生成")
        << "\n\n";

    // ---- 2. 阶段时间线 ----
    out << "## 2. 阶段时间线\n\n";
    out << formatJointTimeline(snapshot) << "\n";

    // ---- 3. 词法结果 ----
    out << "## 3. 词法结果\n\n";
    out << "- Token 数：" << tokens.size() << "\n";
    out << "- ERROR 数：" << errorTokens << "\n";
    out << "- 登记项：" << snapshot.parse.registry.orderedTypes().size()
        << "（含 EOF/ERROR 保留项）\n";
    if (!snapshot.lex.exePath.empty()) {
        out << "- 生成扫描器：" << snapshot.lex.exePath << "\n";
    }
    if (!snapshot.runProducedLex && !snapshot.lex.previewLexText.empty()) {
        out << "- **注意**：以下 token 来自进程内预览，未进入语法分析。\n";
    }
    out << "\n### 3.1 Token 表\n\n";
    if (tokens.empty()) {
        out << "（未执行词法阶段或 sample.lex 不可读）\n\n";
    } else {
        out << "| # | code | name | 类别 | 词素 | 顺序 |\n";
        out << "| --- | --- | --- | --- | --- | --- |\n";
        for (size_t i = 0; i < tokens.size(); ++i) {
            const Token& t = tokens[i];
            out << "| " << (i + 1) << " | " << t.code << " | " << cell(t.name)
                << " | " << kindText(t.kind) << " | " << cell(t.lexeme)
                << " | " << t.loc.line << " |\n";
        }
        out << "\n";
    }

    // ---- 4. 语法结果 ----
    out << "## 4. 语法结果\n\n";
    if (g.productions.empty()) {
        out << "（未执行语法阶段）\n\n";
    } else {
        out << "- 化简后文法：" << g.productions.size() << " 条产生式、"
            << g.nonterminalIds().size() << " 个非终结符\n";
        out << "- LR(0) 状态：" << snapshot.parse.lr0.states.size() << "\n";
        out << "- LALR(1) 状态：" << snapshot.parse.lalr1.states.size()
            << "（规范 LR(1) 中间态 " << snapshot.parse.lalr1.intermediateLr1States.size()
            << "）\n";
        out << "- 分析表冲突：" << conflictCount << "\n\n";

        out << "### 4.1 冲突清单\n\n";
        if (snapshot.parse.parseTable.conflicts.empty()) {
            out << "无冲突。\n\n";
        } else {
            out << "| 状态 | 终结符 | 类型 | 涉及产生式 |\n";
            out << "| --- | --- | --- | --- |\n";
            for (const parser::Conflict& c : snapshot.parse.parseTable.conflicts) {
                std::string prods;
                for (int pid : c.productions) {
                    if (!prods.empty()) {
                        prods += ", ";
                    }
                    prods += std::to_string(pid);
                }
                out << "| " << c.state << " | " << cell(symbolNameOf(g, c.terminal))
                    << " | " << cell(c.type) << " | " << prods << " |\n";
            }
            out << "\n";
        }

        out << "### 4.2 语法树\n\n";
        if (tree) {
            out << "```text\n" << pipeline::formatParseTree(*tree) << "```\n\n";
        } else {
            out << "（未生成语法树）\n\n";
        }

        out << "### 4.3 四元式 IR\n\n";
        if (quadCount > 0) {
            out << "```text\n" << pipeline::formatIrProgram(snapshot.parse.ir) << "```\n\n";
        } else {
            out << "（未生成 IR）\n\n";
        }
    }

    // ---- 5. 覆盖矩阵 ----
    out << "## 5. 覆盖矩阵\n\n";
    if (g.productions.empty()) {
        out << "（未执行语法阶段，无法计算覆盖）\n\n";
    } else {
        out << "- 关键字：" << coverageSummary(coverage.keywords) << "\n";
        out << "- 符号：" << coverageSummary(coverage.symbols) << "\n";
        out << "- 产生式：" << coverageSummary(coverage.productions) << "\n\n";

        const auto writeTable = [&out](const char* title,
                                       const std::vector<CoverageItem>& items) {
            out << "### " << title << "\n\n";
            if (items.empty()) {
                out << "（无）\n\n";
                return;
            }
            out << "| 项 | 覆盖 |\n| --- | --- |\n";
            for (const CoverageItem& it : items) {
                const char* status = it.epsilon ? "ε（无树节点）"
                                                : (it.covered ? "✓" : "—");
                out << "| " << cell(it.label) << " | " << status << " |\n";
            }
            out << "\n";
        };
        writeTable("5.1 关键字", coverage.keywords);
        writeTable("5.2 符号", coverage.symbols);
        writeTable("5.3 产生式", coverage.productions);
    }

    // ---- 6. 诊断汇总 ----
    out << "## 6. 诊断汇总\n\n";
    if (snapshot.diagnostics.empty()) {
        out << "无诊断。\n\n";
    } else {
        out << "| 级别 | 阶段 | 位置 | 消息 |\n";
        out << "| --- | --- | --- | --- |\n";
        for (const Diagnostic& d : snapshot.diagnostics) {
            const std::string loc =
                d.loc.line >= 1 ? ("行 " + std::to_string(d.loc.line)) : "-";
            out << "| " << severityText(d.severity) << " | " << cell(d.stage)
                << " | " << loc << " | " << cell(d.message) << " |\n";
        }
        out << "\n";
    }

    // ---- 7. 说明 ----
    out << "## 7. 说明\n\n";
    out << "- `sample.lex` 不携带源程序行列号，Token 表的「顺序」为数据区序号（1 基）。\n";
    out << "- 规格歧义按既定决策保留：`.` 仅作 NUM 小数点（另有 `SYM_DOT` 独立编码，"
           "见 `test_lex_edge.minic`）；单独 `_` 归入 ID。\n";
    out << "- ε 产生式结构可达但归约不产生树节点，覆盖表中单列为「ε（无树节点）」。\n";

    return out.str();
}

// ---- worker ----

Task3PipelineWorker::Task3PipelineWorker(QObject* parent)
    : QObject(parent)
    , joint_(backend_)
{
    qRegisterMetaType<pipeline::JointSnapshot>("compiler::pipeline::JointSnapshot");

    joint_.setStopCallback([this]() { return stop_.load(); });
    joint_.setStageCallback([this](JointStage s) {
        emit stageStarted(QStringLiteral("联合"), QString::fromUtf8(jointStageLabel(s)));
    });
    joint_.setLexStageCallback([this](Stage s) {
        emit stageStarted(QStringLiteral("词法"), QString::fromUtf8(lexStageLabel(s)));
    });
    joint_.setParseStageCallback([this](ParseStage s) {
        emit stageStarted(QStringLiteral("语法"), QString::fromUtf8(parseStageLabel(s)));
    });
}

Task3PipelineWorker::~Task3PipelineWorker() = default;

void Task3PipelineWorker::requestStop()
{
    stop_.store(true);
}

bool Task3PipelineWorker::stopRequested() const
{
    return stop_.load();
}

void Task3PipelineWorker::applyInputs(const QString& rulesText, const QString& rulesPath,
                                      const QString& bnfText, const QString& bnfPath,
                                      const QString& sourceText,
                                      const QString& outputDir)
{
    // 路径仅用于语言名派生与报告展示；文本内容未变时子流水线内部短路，不会误失效
    joint_.setRulesPath(rulesPath.toStdString());
    joint_.setRulesText(rulesText.toStdString());
    joint_.setBnfPath(bnfPath.toStdString());
    joint_.setBnfText(bnfText.toStdString());
    joint_.setOutputDir(outputDir.toStdString());

    // 统一以文本注入：LexerPipeline 会落盘 <outputDir>/<lang>/_input_src.txt。
    // 不用 setSourcePath（该接口在「先读文件、后改文本」时保留旧路径，会扫描旧文件）。
    joint_.setSourceText(sourceText.toStdString());
}

void Task3PipelineWorker::runTask(int mode, int stage,
                                  const QString& rulesText, const QString& rulesPath,
                                  const QString& bnfText, const QString& bnfPath,
                                  const QString& sourceText,
                                  const QString& outputDir)
{
    QElapsedTimer timer;
    timer.start();

    if (stop_.load()) {
        emit finished(false, joint_.snapshot(), 0);
        return;
    }

    applyInputs(rulesText, rulesPath, bnfText, bnfPath, sourceText, outputDir);

    bool ok = false;
    switch (static_cast<JointRunMode>(mode)) {
    case JointRunMode::Joint:
        ok = joint_.runThrough(static_cast<JointStage>(stage));
        break;
    case JointRunMode::Lexer:
        ok = joint_.runLexerThrough(static_cast<Stage>(stage));
        break;
    case JointRunMode::Parser:
        ok = joint_.runParserThrough(static_cast<ParseStage>(stage));
        break;
    case JointRunMode::Preset:
        // 独立发布环境（可能无 g++）：不碰生成扫描器，直接载入随包分发的
        // <outputDir>/<语言名>/sample.lex，随后即可续跑语法与 IR
        ok = joint_.loadPresetLexFromOutputDir();
        if (ok) {
            emit logMessage(QStringLiteral(
                "[Info] 已载入预置 sample.lex，语法与中间代码阶段可直接运行"));
        }
        break;
    default:
        emit logMessage(QStringLiteral("[Error] [joint] 未知运行模式"));
        ok = false;
        break;
    }

    emit finished(ok, joint_.snapshot(), timer.elapsed());
}

} // namespace compiler::gui
