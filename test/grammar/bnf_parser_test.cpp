// T2.1 单测：BnfParser（.bnf → Grammar）+ 终结符→tokenCode 映射（备 B：重解析 .rules）。
// 零 Qt、纯 STL；沿用 T1.x 冒烟约定：g++ 独立编译运行，退出码 0 = 全部通过。
// 用法：bnf_parser_test <languages 根目录>（缺省 ./test/languages）
//
// 覆盖（docs/TASKS.md T2.1 验收）：
//   正例：tiny.bnf（理论值 17 非终结符 / 26 终结符 / 37 产生式）、minic.bnf（5 条 ε 产生式）、
//         引号字面量 → 登记名、@start 缺失缺省 + Info、'$' → EndOfFile、多行续写、
//         行首 '#' 注释、BOM、@start 位置无关
//   负例：未定义终结符 / @start 未定义 / @start 指向终结符 / 重复 @start / 缺 ';' /
//         尾随 '|' 空候选 / 前导 '|' / 右部全空 / 空文法 / 引号未闭合 /
//         字面量 display 不匹配 / '#' 与其他符号并列
//
// 说明：tiny.bnf 数字（17/26/37）先按理论值断言，运行后若与实际不符再调整（PROGRESS 流程）。

#include "BnfParser.h"
#include "LexRulesParser.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace {

int g_checks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            std::cerr << "FAIL @" << __LINE__ << ": " << #cond << std::endl; \
            return false;                                                    \
        }                                                                    \
    } while (0)

std::string loadFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

// 备 B：重解析 .rules → TokenCodeRegistry（T2 决策点 2/5：主 A 备 B，先 B 后 A）
bool registryFromRules(const std::string& rulesPath,
                       compiler::TokenCodeRegistry& out)
{
    compiler::lexer::LexRulesParser parser;
    auto r = parser.parseFile(rulesPath);
    if (!r.ok) {
        std::cerr << "rules 解析失败: " << rulesPath << std::endl;
        for (const auto& d : r.diagnostics) {
            std::cerr << "  " << d.message << std::endl;
        }
        return false;
    }
    out = r.value.registry; // TokenCodeRegistry 为值成员，可拷贝
    return true;
}

bool hasError(const compiler::Result<compiler::Grammar>& r)
{
    for (const auto& d : r.diagnostics) {
        if (d.severity == compiler::Severity::Error) {
            return true;
        }
    }
    return false;
}

bool hasInfo(const compiler::Result<compiler::Grammar>& r)
{
    for (const auto& d : r.diagnostics) {
        if (d.severity == compiler::Severity::Info) {
            return true;
        }
    }
    return false;
}

// 在 stage="bnf-parse" 的错误里查找包含 needle 的消息
const compiler::Diagnostic* findBnfError(
    const compiler::Result<compiler::Grammar>& r, const std::string& needle)
{
    for (const auto& d : r.diagnostics) {
        if (d.severity == compiler::Severity::Error &&
            d.stage == "bnf-parse" &&
            d.message.find(needle) != std::string::npos) {
            return &d;
        }
    }
    return nullptr;
}

// ---------- 正例：tiny.bnf ----------
bool testTiny(const std::string& langRoot)
{
    compiler::TokenCodeRegistry reg;
    if (!registryFromRules(langRoot + "/tiny/tiny.rules", reg)) {
        return false;
    }

    compiler::grammar::BnfParser parser;
    auto r = parser.parseFile(langRoot + "/tiny/tiny.bnf", &reg);
    CHECK(r.ok);
    CHECK(!hasError(r));

    const compiler::Grammar& g = r.value;

    // 内部符号（DATA_STRUCTURES §5.1）
    CHECK(g.symbols.size() >= 2);
    CHECK(g.symbol(0)->name == "$");
    CHECK(g.symbol(0)->kind == compiler::SymbolKind::EndOfFile);
    CHECK(g.symbol(0)->tokenCode == 0);
    CHECK(g.symbol(1)->name == "#");
    CHECK(g.symbol(1)->kind == compiler::SymbolKind::Empty);
    CHECK(g.symbol(1)->tokenCode == -1);

    // 开始符号
    CHECK(g.startSymbol >= 0);
    CHECK(g.symbol(g.startSymbol)->name == "program");
    CHECK(g.symbol(g.startSymbol)->kind == compiler::SymbolKind::Nonterminal);

    // 规模（理论值：17 非终结符 / 26 终结符 / 37 产生式；运行后如有出入调整断言）
    const size_t nt = g.nonterminalIds().size();
    const size_t t = g.terminalIds().size();
    const size_t p = g.productions.size();
    std::cout << "tiny: symbols=" << g.symbols.size()
              << " nonterm=" << nt << " term=" << t
              << " productions=" << p << std::endl;
    CHECK(nt == 17);
    CHECK(t == 26);
    CHECK(p == 37);

    // 抽查关键产生式：program -> stmt-sequence
    CHECK(g.productions[0].lhs ==
          g.symbolId("program", compiler::SymbolKind::Nonterminal));
    CHECK(g.productions[0].rhs.size() == 1);
    CHECK(g.productions[0].rhs[0] ==
          g.symbolId("stmt-sequence", compiler::SymbolKind::Nonterminal));

    // 全部终结符 tokenCode 与 registry 一致
    for (int tid : g.terminalIds()) {
        const compiler::Symbol* s = g.symbol(tid);
        CHECK(s->tokenCode >= 0);
        CHECK(s->tokenCode == reg.codeOf(s->name));
    }

    // 每个非终结符都出现在某个左部
    std::set<int> lhsIds;
    for (const auto& pr : g.productions) {
        lhsIds.insert(pr.lhs);
    }
    for (int nid : g.nonterminalIds()) {
        CHECK(lhsIds.count(nid) == 1);
    }
    return true;
}

// ---------- 正例：minic.bnf ----------
bool testMinic(const std::string& langRoot)
{
    compiler::TokenCodeRegistry reg;
    if (!registryFromRules(langRoot + "/minic/minic.rules", reg)) {
        return false;
    }

    compiler::grammar::BnfParser parser;
    auto r = parser.parseFile(langRoot + "/minic/minic.bnf", &reg);
    CHECK(r.ok);
    CHECK(!hasError(r));

    const compiler::Grammar& g = r.value;
    CHECK(g.symbol(g.startSymbol)->name == "program");

    // ε 产生式（rhs = {}）：local-definitions / statement-list / for-init / for-update / arguments
    std::set<std::string> emptyLhs;
    for (const auto& pr : g.productions) {
        if (pr.rhs.empty()) {
            emptyLhs.insert(g.symbol(pr.lhs)->name);
        }
    }
    std::cout << "minic: empty-productions=" << emptyLhs.size()
              << " nonterm=" << g.nonterminalIds().size()
              << " term=" << g.terminalIds().size()
              << " productions=" << g.productions.size() << std::endl;
    CHECK(emptyLhs.size() == 5);
    CHECK(emptyLhs.count("local-definitions") == 1);
    CHECK(emptyLhs.count("statement-list") == 1);
    CHECK(emptyLhs.count("for-init") == 1);
    CHECK(emptyLhs.count("for-update") == 1);
    CHECK(emptyLhs.count("arguments") == 1);

    // 全部终结符可解析且与 registry 一致
    for (int tid : g.terminalIds()) {
        const compiler::Symbol* s = g.symbol(tid);
        CHECK(s->tokenCode >= 0);
        CHECK(s->tokenCode == reg.codeOf(s->name));
    }
    return true;
}

// ---------- 正例：格式细节 ----------
bool testFormatDetails(const std::string& langRoot)
{
    compiler::TokenCodeRegistry reg;
    if (!registryFromRules(langRoot + "/tiny/tiny.rules", reg)) {
        return false;
    }
    compiler::grammar::BnfParser parser;

    // 引号字面量 → 登记名 + tokenCode
    {
        auto r = parser.parse("@start s\ns -> 'if' ID ;\n", &reg);
        CHECK(r.ok);
        const compiler::Grammar& g = r.value;
        const int kwIf = g.symbolId("KW_IF", compiler::SymbolKind::Terminal);
        CHECK(kwIf >= 0);
        CHECK(g.symbol(kwIf)->tokenCode == reg.codeOf("KW_IF"));
        CHECK(g.symbol(kwIf)->tokenCode == 103);
        CHECK(g.productions[0].rhs.size() == 2);
        CHECK(g.productions[0].rhs[0] == kwIf);
    }

    // 双引号字面量
    {
        auto r = parser.parse("@start s\ns -> \"then\" NUM ;\n", &reg);
        CHECK(r.ok);
        CHECK(r.value.symbolId("KW_THEN", compiler::SymbolKind::Terminal) >= 0);
    }

    // @start 缺失 → 缺省第一条左部 + Info（FILE_FORMATS §3.2）；无 registry → tokenCode=-1
    {
        auto r = parser.parse("a -> b ;\n", nullptr);
        CHECK(r.ok);
        CHECK(r.value.startSymbol ==
              r.value.symbolId("a", compiler::SymbolKind::Nonterminal));
        CHECK(hasInfo(r));
        const int b = r.value.symbolId("b", compiler::SymbolKind::Terminal);
        CHECK(b >= 0);
        CHECK(r.value.symbol(b)->tokenCode == -1);
    }

    // '$' → 内部符号 id 0（EndOfFile）
    {
        auto r = parser.parse("a -> b $ ;\n", nullptr);
        CHECK(r.ok);
        CHECK(r.value.productions[0].rhs.size() == 2);
        CHECK(r.value.productions[0].rhs[1] == 0);
        CHECK(r.value.symbol(0)->kind == compiler::SymbolKind::EndOfFile);
    }

    // 多行续写（'|' 位于续行行首）
    {
        auto r = parser.parse("@start s\ns -> a\n   |  b ;\n", nullptr);
        CHECK(r.ok);
        CHECK(r.value.productions.size() == 2);
        const int s = r.value.symbolId("s", compiler::SymbolKind::Nonterminal);
        CHECK(r.value.productions[0].lhs == s);
        CHECK(r.value.productions[1].lhs == s);
    }

    // 行首 '#' = 注释（含本会被误读为产生式的行）
    {
        auto r = parser.parse("# 注释：broken -> production ;\ns -> a ;\n", nullptr);
        CHECK(r.ok);
        CHECK(r.value.productions.size() == 1);
    }

    // 产生式内的 '#' = 空串（与 '|' 同行）→ ε 产生式 rhs={}
    {
        auto r = parser.parse("s -> a | # ;\n", nullptr);
        CHECK(r.ok);
        CHECK(r.value.productions.size() == 2);
        CHECK(r.value.productions[1].rhs.empty());
    }

    // UTF-8 BOM
    {
        const std::string bom("\xEF\xBB\xBF", 3);
        auto r = parser.parse(bom + "s -> a ;\n", nullptr);
        CHECK(r.ok);
    }

    // @start 在产生式之后出现（位置无关）
    {
        auto r = parser.parse("s -> a ;\n@start s\n", nullptr);
        CHECK(r.ok);
        CHECK(r.value.startSymbol ==
              r.value.symbolId("s", compiler::SymbolKind::Nonterminal));
    }
    return true;
}

// ---------- 负例：错误诊断 ----------
bool testNegative(const std::string& langRoot)
{
    compiler::TokenCodeRegistry reg;
    if (!registryFromRules(langRoot + "/tiny/tiny.rules", reg)) {
        return false;
    }
    compiler::grammar::BnfParser parser;

    // 终结符未定义（registry 中无 FOO）：stage="bnf-parse"、1 基行列号
    {
        auto r = parser.parse("s -> FOO ;\n", &reg);
        CHECK(!r.ok);
        const compiler::Diagnostic* d = findBnfError(r, "FOO");
        CHECK(d != nullptr);
        CHECK(d->loc.line == 1);
        CHECK(d->loc.column == 6);
    }

    // @start 指向未定义符号
    {
        auto r = parser.parse("@start nosuch\ns -> a ;\n", nullptr);
        CHECK(!r.ok);
        CHECK(findBnfError(r, "nosuch") != nullptr);
    }

    // @start 指向终结符（仅右部出现）
    {
        auto r = parser.parse("@start a\ns -> a ;\n", nullptr);
        CHECK(!r.ok);
    }

    // 重复 @start
    {
        auto r = parser.parse("@start s\n@start t\ns -> a ;\n", nullptr);
        CHECK(!r.ok);
        CHECK(findBnfError(r, "重复") != nullptr);
    }

    // 缺少 ';'
    {
        auto r = parser.parse("s -> a\nt -> b ;\n", nullptr);
        CHECK(!r.ok);
        CHECK(findBnfError(r, "';'") != nullptr);
    }

    // 尾随 '|'：空候选未显式 '#'
    {
        auto r = parser.parse("s -> a | ;\n", nullptr);
        CHECK(!r.ok);
        CHECK(findBnfError(r, "'#'") != nullptr);
    }

    // 前导 '|'
    {
        auto r = parser.parse("s -> | a ;\n", nullptr);
        CHECK(!r.ok);
    }

    // 右部完全为空
    {
        auto r = parser.parse("s -> ;\n", nullptr);
        CHECK(!r.ok);
    }

    // 空文法
    {
        auto r = parser.parse("", nullptr);
        CHECK(!r.ok);
        CHECK(findBnfError(r, "文法为空") != nullptr);
    }

    // 引号未闭合
    {
        auto r = parser.parse("s -> 'a ;\n", nullptr);
        CHECK(!r.ok);
        CHECK(findBnfError(r, "未闭合") != nullptr);
    }

    // 字面量 display 不匹配
    {
        auto r = parser.parse("s -> 'zzz' ;\n", &reg);
        CHECK(!r.ok);
        CHECK(findBnfError(r, "zzz") != nullptr);
    }

    // '#' 与其他符号并列（# 在前 / 在后）
    {
        auto r = parser.parse("s -> # a ;\n", nullptr);
        CHECK(!r.ok);
    }
    {
        auto r = parser.parse("s -> a # ;\n", nullptr);
        CHECK(!r.ok);
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    std::string langRoot = "./test/languages";
    if (argc > 1) {
        langRoot = argv[1];
    }

    const std::string tinyBnf = loadFile(langRoot + "/tiny/tiny.bnf");
    const std::string minicBnf = loadFile(langRoot + "/minic/minic.bnf");
    if (tinyBnf.empty() || minicBnf.empty()) {
        std::cerr << "FAIL: 找不到语言包（" << langRoot
                  << "/tiny|minic/*.bnf）" << std::endl;
        return 1;
    }

    bool ok = true;
    ok &= testTiny(langRoot);
    ok &= testMinic(langRoot);
    ok &= testFormatDetails(langRoot);
    ok &= testNegative(langRoot);

    if (!ok) {
        std::cerr << "FAIL: 存在未通过的 CHECK（已通过 " << g_checks
                  << " 项检查）" << std::endl;
        return 1;
    }
    std::cout << "bnf_parser_test OK, checks=" << g_checks << std::endl;
    return 0;
}
