// T2.2 单测：GrammarSimplifier（文法化简：删除无用/不可达符号与产生式）。
// 零 Qt、纯 STL；沿用 T1.x/T2.1 冒烟约定：g++ 独立编译运行，退出码 0 = 全部通过。
// 用法：grammar_simplifier_test <languages 根目录>（缺省 ./test/languages）
//
// 覆盖（docs/PROGRESS.md 决策点 3 + 方案 D）：
//   no-op 基线：tiny.bnf（37 产生式 / 45 符号）、minic.bnf（78 产生式 / 5 条 ε 保留）
//   人造文法：无用+不可达混合（日志顺序/原因）、全是无用（报错 stage=simplify）、
//             startSymbol 缺失、单产生式（含单 ε）、ε 保留 + 悬垂引用回归（#7）、
//             孤儿终结符、组合 BnfParser→Simplifier 且 tokenCode 保留、
//             重编号不变量（$/# 固定槽、id 稠密）、幂等。

#include "BnfParser.h"
#include "GrammarSimplifier.h"
#include "LexRulesParser.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

using compiler::SymbolKind;
using compiler::grammar::BnfParser;
using compiler::grammar::GrammarSimplifier;
using compiler::grammar::SimplifiedGrammar;

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

// 备 B：重解析 .rules → TokenCodeRegistry（与 bnf_parser_test 一致）
bool registryFromRules(const std::string& rulesPath,
                       compiler::TokenCodeRegistry& out)
{
    compiler::lexer::LexRulesParser parser;
    auto r = parser.parseFile(rulesPath);
    if (!r.ok) {
        std::cerr << "rules 解析失败: " << rulesPath << std::endl;
        return false;
    }
    out = r.value.registry;
    return true;
}

compiler::Result<compiler::Grammar> parseText(const std::string& text)
{
    return BnfParser().parse(text, nullptr);
}

// 手工构造 Grammar（满足 BnfParser 不变式：symbols[i].id==i、productions[i].id==i；
// 预置 $=0 / #=1；用户符号自 id 2 起）。
// userSymbols: {name, kind}；productions: {lhs, rhs...}（单元素 = ε 产生式）。
compiler::Grammar makeHandGrammar(
    const std::vector<std::pair<std::string, SymbolKind>>& userSymbols,
    const std::vector<std::vector<int>>& productions, int startSymbol)
{
    compiler::Grammar g;
    g.symbols.push_back({0, "$", SymbolKind::EndOfFile, 0});
    g.symbols.push_back({1, "#", SymbolKind::Empty, -1});
    for (size_t i = 0; i < userSymbols.size(); ++i) {
        g.symbols.push_back({static_cast<int>(i + 2), userSymbols[i].first,
                             userSymbols[i].second, -1});
    }
    for (size_t i = 0; i < productions.size(); ++i) {
        compiler::Production p;
        p.id = static_cast<int>(i);
        if (productions[i].empty()) {
            continue;   // 防御：测试不会构造空条目
        }
        p.lhs = productions[i][0];
        for (size_t k = 1; k < productions[i].size(); ++k) {
            p.rhs.push_back(productions[i][k]);
        }
        g.productions.push_back(std::move(p));
    }
    g.startSymbol = startSymbol;
    return g;
}

// 在 simplify 结果里找 stage 匹配的 Error
bool hasStageError(const compiler::Result<SimplifiedGrammar>& r,
                   const std::string& stage)
{
    for (const auto& d : r.diagnostics) {
        if (d.severity == compiler::Severity::Error && d.stage == stage) {
            return true;
        }
    }
    return false;
}

// D-1：tiny.bnf no-op 基线
bool testTinyNoop(const std::string& langRoot)
{
    auto r = BnfParser().parseFile(langRoot + "/tiny/tiny.bnf", nullptr);
    CHECK(r.ok);
    const size_t symBefore = r.value.symbols.size();
    const size_t prodBefore = r.value.productions.size();

    auto sg = GrammarSimplifier().simplify(r.value);
    CHECK(sg.ok);
    CHECK(sg.value.log.empty());
    CHECK(sg.value.grammar.symbols.size() == symBefore);
    CHECK(sg.value.grammar.productions.size() == prodBefore);
    CHECK(sg.value.grammar.symbols.size() == 45);      // 2 内部 + 17 非终结符 + 26 终结符
    CHECK(sg.value.grammar.productions.size() == 37);
    CHECK(sg.value.grammar.startSymbol ==
          sg.value.grammar.symbolId("program", SymbolKind::Nonterminal));
    return true;
}

// D-2：minic.bnf no-op 基线（含 5 条 ε 保留）
bool testMinicNoop(const std::string& langRoot)
{
    auto r = BnfParser().parseFile(langRoot + "/minic/minic.bnf", nullptr);
    CHECK(r.ok);
    const size_t symBefore = r.value.symbols.size();
    const size_t prodBefore = r.value.productions.size();

    auto sg = GrammarSimplifier().simplify(r.value);
    CHECK(sg.ok);
    CHECK(sg.value.log.empty());
    CHECK(sg.value.grammar.symbols.size() == symBefore);
    CHECK(sg.value.grammar.productions.size() == prodBefore);
    CHECK(sg.value.grammar.productions.size() == 78);
    size_t eps = 0;
    for (const auto& p : sg.value.grammar.productions) {
        if (p.rhs.empty()) {
            ++eps;
        }
    }
    CHECK(eps == 5);   // local-definitions / statement-list / for-init / for-update / arguments
    return true;
}

// D-3：人造文法——无用（C、D 成环无终结符）+ 不可达（B 可终结但不可达）混合
bool testUselessAndUnreachable()
{
    auto r = parseText(
        "@start S\n"
        "S -> A b ;\n"
        "A -> a a ;\n"
        "B -> x x ;\n"
        "C -> D ;\n"
        "D -> C ;\n");
    CHECK(r.ok);

    auto sg = GrammarSimplifier().simplify(r.value);
    CHECK(sg.ok);
    const auto& log = sg.value.log;

    // 产生式日志：先 useless（原始 id 3=C->D、4=D->C），后 unreachable（原始 id 2=B->xx）
    CHECK(log.removedProductions.size() == 3);
    if (log.removedProductions.size() == 3) {
        CHECK(log.removedProductions[0].originalId == 3);
        CHECK(log.removedProductions[0].reason == "useless");
        CHECK(log.removedProductions[1].originalId == 4);
        CHECK(log.removedProductions[1].reason == "useless");
        CHECK(log.removedProductions[2].originalId == 2);
        CHECK(log.removedProductions[2].reason == "unreachable");
    }
    // 符号日志：先 useless（C、D），后 unreachable（B）；孤儿终结符 x 不单列
    CHECK(log.removedSymbols.size() == 3);
    if (log.removedSymbols.size() == 3) {
        CHECK(log.removedSymbols[0].name == "C");
        CHECK(log.removedSymbols[0].reason == "useless");
        CHECK(log.removedSymbols[1].name == "D");
        CHECK(log.removedSymbols[1].reason == "useless");
        CHECK(log.removedSymbols[2].name == "B");
        CHECK(log.removedSymbols[2].reason == "unreachable");
    }

    const auto& g = sg.value.grammar;
    CHECK(g.productions.size() == 2);
    CHECK(g.startSymbol == g.symbolId("S", SymbolKind::Nonterminal));
    CHECK(g.symbolId("A", SymbolKind::Nonterminal) >= 0);
    CHECK(g.symbolId("B", SymbolKind::Nonterminal) < 0);
    CHECK(g.symbolId("C", SymbolKind::Nonterminal) < 0);
    CHECK(g.symbolId("D", SymbolKind::Nonterminal) < 0);
    CHECK(g.symbolId("x", SymbolKind::Terminal) < 0);   // 孤儿终结符从符号表移除
    return true;
}

// D-4：全是无用符号 → 报错 stage="simplify"
bool testAllUseless()
{
    auto r = parseText("@start S\nS -> T ;\nT -> S ;\n");
    CHECK(r.ok);
    auto sg = GrammarSimplifier().simplify(r.value);
    CHECK(!sg.ok);
    CHECK(hasStageError(sg, "simplify"));
    return true;
}

// D-5：startSymbol 缺失
bool testMissingStart()
{
    auto g = makeHandGrammar(
        { {"S", SymbolKind::Nonterminal}, {"a", SymbolKind::Terminal} },
        { {2, 3} },   // S -> a
        -1);          // startSymbol 缺失
    auto sg = GrammarSimplifier().simplify(g);
    CHECK(!sg.ok);
    CHECK(hasStageError(sg, "simplify"));
    return true;
}

// D-6：只有一条产生式（含单 ε 变体）
bool testSingleProduction()
{
    // S -> a
    auto g1 = makeHandGrammar(
        { {"S", SymbolKind::Nonterminal}, {"a", SymbolKind::Terminal} },
        { {2, 3} }, 2);
    auto sg1 = GrammarSimplifier().simplify(g1);
    CHECK(sg1.ok);
    CHECK(sg1.value.log.empty());
    CHECK(sg1.value.grammar.productions.size() == 1);

    // S -> ε
    auto g2 = makeHandGrammar(
        { {"S", SymbolKind::Nonterminal} },
        { {2} }, 2);
    auto sg2 = GrammarSimplifier().simplify(g2);
    CHECK(sg2.ok);
    CHECK(sg2.value.log.empty());
    CHECK(sg2.value.grammar.productions.size() == 1);
    CHECK(sg2.value.grammar.productions[0].rhs.empty());
    return true;
}

// D-7：ε 保留 + 悬垂引用回归（方案 Bug 修正的复现用例）
//   S -> # | T ; T -> T
//   S 经 S-># 是 productive；T 非 productive。S->T 引用 T 必须一并删除，
//   不能因"左部 S 是 productive"而保留（否则悬垂引用）。
bool testEpsilonKept()
{
    auto r = parseText("@start S\nS -> # | T ;\nT -> T ;\n");
    CHECK(r.ok);
    auto sg = GrammarSimplifier().simplify(r.value);
    CHECK(sg.ok);
    const auto& log = sg.value.log;

    // 原始 id：0=S->#、1=S->T、2=T->T
    CHECK(log.removedProductions.size() == 2);
    if (log.removedProductions.size() == 2) {
        CHECK(log.removedProductions[0].originalId == 1);
        CHECK(log.removedProductions[0].reason == "useless");
        CHECK(log.removedProductions[1].originalId == 2);
        CHECK(log.removedProductions[1].reason == "useless");
    }
    CHECK(log.removedSymbols.size() == 1);
    if (log.removedSymbols.size() == 1) {
        CHECK(log.removedSymbols[0].name == "T");
        CHECK(log.removedSymbols[0].reason == "useless");
    }

    const auto& g = sg.value.grammar;
    CHECK(g.productions.size() == 1);
    CHECK(g.productions[0].rhs.empty());                    // 仅 S -> #（ε 保留）
    CHECK(g.startSymbol == g.symbolId("S", SymbolKind::Nonterminal));
    CHECK(g.symbolId("T", SymbolKind::Nonterminal) < 0);
    // 无悬垂引用：剩余产生式右部每个符号都能解析
    for (const auto& p : g.productions) {
        CHECK(g.symbol(p.lhs) != nullptr);
        for (int s : p.rhs) {
            CHECK(g.symbol(s) != nullptr);
        }
    }
    return true;
}

// D-8：孤儿终结符（未出现在任何产生式右部）从符号表移除，但不单列日志
bool testOrphanTerminal()
{
    auto g = makeHandGrammar(
        { {"S", SymbolKind::Nonterminal},
          {"a", SymbolKind::Terminal},
          {"z", SymbolKind::Terminal} },   // z 未被任何产生式引用
        { {2, 3} },                         // S -> a
        2);
    auto sg = GrammarSimplifier().simplify(g);
    CHECK(sg.ok);
    CHECK(sg.value.log.empty());                       // 孤儿终结符不记日志
    CHECK(sg.value.grammar.productions.size() == 1);
    CHECK(sg.value.grammar.symbolId("a", SymbolKind::Terminal) >= 0);
    CHECK(sg.value.grammar.symbolId("z", SymbolKind::Terminal) < 0);
    return true;
}

// D-9：组合测试 BnfParser → GrammarSimplifier，tokenCode 经重编号后保留
bool testCombinedWithRegistry(const std::string& langRoot)
{
    compiler::TokenCodeRegistry reg;
    CHECK(registryFromRules(langRoot + "/tiny/tiny.rules", reg));

    auto r = BnfParser().parseFile(langRoot + "/tiny/tiny.bnf", &reg);
    CHECK(r.ok);
    auto sg = GrammarSimplifier().simplify(r.value);
    CHECK(sg.ok);
    CHECK(sg.value.log.empty());

    const auto& g = sg.value.grammar;
    const int plus = g.symbolId("SYM_PLUS", SymbolKind::Terminal);
    CHECK(plus >= 0);
    if (plus >= 0) {
        const compiler::Symbol* s = g.symbol(plus);
        CHECK(s != nullptr);
        if (s) {
            CHECK(s->tokenCode == reg.codeOf("SYM_PLUS"));
        }
    }
    CHECK(g.startSymbol == g.symbolId("program", SymbolKind::Nonterminal));
    return true;
}

// D-10：重编号不变量——$/# 固定槽、id 稠密、所有引用可解析
bool testRenumberInvariant()
{
    auto r = parseText(
        "@start S\n"
        "S -> A b ;\n"
        "A -> a a ;\n"
        "B -> x x ;\n"
        "C -> D ;\n"
        "D -> C ;\n");
    CHECK(r.ok);
    auto sg = GrammarSimplifier().simplify(r.value);
    CHECK(sg.ok);

    const auto& g = sg.value.grammar;
    for (size_t i = 0; i < g.symbols.size(); ++i) {
        CHECK(g.symbols[i].id == static_cast<int>(i));
    }
    for (size_t i = 0; i < g.productions.size(); ++i) {
        CHECK(g.productions[i].id == static_cast<int>(i));
    }
    CHECK(g.symbols[0].kind == SymbolKind::EndOfFile);   // $
    CHECK(g.symbols[1].kind == SymbolKind::Empty);       // #
    for (const auto& p : g.productions) {
        CHECK(g.symbol(p.lhs) != nullptr);
        for (int s : p.rhs) {
            CHECK(g.symbol(s) != nullptr);
        }
    }
    CHECK(g.startSymbol >= 0 && g.symbol(g.startSymbol) != nullptr);
    return true;
}

// D-11：幂等——二次化简不再删除，结果与首次一致
bool testIdempotent()
{
    auto r = parseText(
        "@start S\n"
        "S -> A b | C ;\n"
        "A -> a ;\n"
        "C -> D ;\n"
        "D -> C ;\n"
        "B -> b b ;\n");
    CHECK(r.ok);

    GrammarSimplifier simp;
    auto first = simp.simplify(r.value);
    CHECK(first.ok);
    auto second = simp.simplify(first.value.grammar);
    CHECK(second.ok);
    CHECK(second.value.log.empty());   // 二次化简 = no-op

    const auto& g1 = first.value.grammar;
    const auto& g2 = second.value.grammar;
    CHECK(g1.symbols.size() == g2.symbols.size());
    CHECK(g1.productions.size() == g2.productions.size());
    for (size_t i = 0; i < g1.productions.size(); ++i) {
        CHECK(g1.productions[i].lhs == g2.productions[i].lhs);
        CHECK(g1.productions[i].rhs == g2.productions[i].rhs);
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
    ok &= testTinyNoop(langRoot);
    ok &= testMinicNoop(langRoot);
    ok &= testUselessAndUnreachable();
    ok &= testAllUseless();
    ok &= testMissingStart();
    ok &= testSingleProduction();
    ok &= testEpsilonKept();
    ok &= testOrphanTerminal();
    ok &= testCombinedWithRegistry(langRoot);
    ok &= testRenumberInvariant();
    ok &= testIdempotent();

    if (!ok) {
        std::cerr << "FAIL: 存在未通过的 CHECK（已通过 " << g_checks
                  << " 项检查）" << std::endl;
        return 1;
    }
    std::cout << "grammar_simplifier_test OK, checks=" << g_checks << std::endl;
    return 0;
}
