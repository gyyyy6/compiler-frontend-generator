// T2.3 单测：FirstFollow（First / Follow / Nullable 集合，不动点）。
// 零 Qt、纯 STL；沿用 T1.x/T2.x 冒烟约定：g++ 独立编译运行，退出码 0 = 全部通过。
// 用法：first_follow_test <languages 根目录>（缺省 ./test/languages）
//
// 覆盖（docs/PROGRESS.md 决策点 + 方案 D）：
//   TINY First/Follow/nullable 理论值、Mini-C 5 个 nullable 与 First(local-definitions) 含 ε、
//   firstOfString 的 ε 剥离修正（核心正确性证据）、边界（单产生式/纯 ε/空文法/无效 start）、
//   Follow 链传播。TINY / Mini-C 用例均走全链路 BnfParser→GrammarSimplifier→FirstFollow。

#include "BnfParser.h"
#include "FirstFollow.h"
#include "GrammarSimplifier.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using compiler::SymbolKind;
using compiler::grammar::BnfParser;
using compiler::grammar::FirstFollow;
using compiler::grammar::FirstFollowSets;
using compiler::grammar::GrammarSimplifier;
using compiler::grammar::kEpsilonSymbolId;

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

bool hasStageError(const compiler::Result<FirstFollowSets>& r,
                   const std::string& stage)
{
    for (const auto& d : r.diagnostics) {
        if (d.severity == compiler::Severity::Error && d.stage == stage) {
            return true;
        }
    }
    return false;
}

int idOf(const compiler::Grammar& g, const std::string& name, SymbolKind kind)
{
    return g.symbolId(name, kind);
}

// D-1/2/3：TINY —— First(program)、Follow(stmt-sequence)、nullable（全链路）
bool testTiny(const std::string& langRoot)
{
    auto r = BnfParser().parseFile(langRoot + "/tiny/tiny.bnf", nullptr);
    CHECK(r.ok);
    auto sg = GrammarSimplifier().simplify(r.value);
    CHECK(sg.ok);
    auto fs = FirstFollow().compute(sg.value.grammar);
    CHECK(fs.ok);
    const auto& g = sg.value.grammar;
    const auto& sets = fs.value;

    // First(program) ⊇ {KW_IF, KW_REPEAT, ID, KW_READ, KW_WRITE}，且不含 ε
    const int program = idOf(g, "program", SymbolKind::Nonterminal);
    CHECK(program >= 0);
    if (program >= 0) {
        const std::set<int>& f = sets.first[static_cast<size_t>(program)];
        CHECK(f.count(idOf(g, "KW_IF", SymbolKind::Terminal)) == 1);
        CHECK(f.count(idOf(g, "KW_REPEAT", SymbolKind::Terminal)) == 1);
        CHECK(f.count(idOf(g, "ID", SymbolKind::Terminal)) == 1);
        CHECK(f.count(idOf(g, "KW_READ", SymbolKind::Terminal)) == 1);
        CHECK(f.count(idOf(g, "KW_WRITE", SymbolKind::Terminal)) == 1);
        CHECK(f.count(kEpsilonSymbolId) == 0);   // tiny 无可空符号

        // 收敛性：First(exp) 大小 == 3（{SYM_LPAREN, NUM, ID}）
        const int exp = idOf(g, "exp", SymbolKind::Nonterminal);
        CHECK(exp >= 0);
        if (exp >= 0) {
            const std::set<int>& fe = sets.first[static_cast<size_t>(exp)];
            CHECK(fe.size() == 3);
            CHECK(fe.count(idOf(g, "SYM_LPAREN", SymbolKind::Terminal)) == 1);
            CHECK(fe.count(idOf(g, "NUM", SymbolKind::Terminal)) == 1);
            CHECK(fe.count(idOf(g, "ID", SymbolKind::Terminal)) == 1);
        }
    }

    // Follow(stmt-sequence) 精确集 == {$, SYM_SEMI, KW_END, KW_ELSE, KW_UNTIL}（大小 5）
    const int ss = idOf(g, "stmt-sequence", SymbolKind::Nonterminal);
    CHECK(ss >= 0);
    if (ss >= 0) {
        const std::set<int>& fo = sets.follow[static_cast<size_t>(ss)];
        CHECK(fo.count(idOf(g, "$", SymbolKind::EndOfFile)) == 1);
        CHECK(fo.count(idOf(g, "SYM_SEMI", SymbolKind::Terminal)) == 1);
        CHECK(fo.count(idOf(g, "KW_END", SymbolKind::Terminal)) == 1);
        CHECK(fo.count(idOf(g, "KW_ELSE", SymbolKind::Terminal)) == 1);
        CHECK(fo.count(idOf(g, "KW_UNTIL", SymbolKind::Terminal)) == 1);
        CHECK(fo.size() == 5);
    }

    // nullable：tiny 无可空符号
    CHECK(!sets.nullable[static_cast<size_t>(program)]);
    CHECK(!sets.nullable[static_cast<size_t>(ss)]);
    return true;
}

// D-4/5：Mini-C —— 5 个 nullable、First(local-definitions) 含 ε（全链路）
bool testMinic(const std::string& langRoot)
{
    auto r = BnfParser().parseFile(langRoot + "/minic/minic.bnf", nullptr);
    CHECK(r.ok);
    auto sg = GrammarSimplifier().simplify(r.value);
    CHECK(sg.ok);
    auto fs = FirstFollow().compute(sg.value.grammar);
    CHECK(fs.ok);
    const auto& g = sg.value.grammar;
    const auto& sets = fs.value;

    // 5 个 ε 产生式对应的非终结符 nullable
    const std::vector<std::string> epsNts = {
        "local-definitions", "statement-list", "for-init", "for-update", "arguments"};
    for (const std::string& name : epsNts) {
        const int id = idOf(g, name, SymbolKind::Nonterminal);
        CHECK(id >= 0);
        if (id >= 0) {
            CHECK(sets.nullable[static_cast<size_t>(id)]);
        }
    }
    // program 不可空（防空可空误判）
    const int program = idOf(g, "program", SymbolKind::Nonterminal);
    CHECK(program >= 0);
    if (program >= 0) {
        CHECK(!sets.nullable[static_cast<size_t>(program)]);
    }

    // First(local-definitions) == {#, KW_INT, KW_FLOAT, KW_DOUBLE, KW_VOID}（大小 5）
    const int ld = idOf(g, "local-definitions", SymbolKind::Nonterminal);
    CHECK(ld >= 0);
    if (ld >= 0) {
        const std::set<int>& f = sets.first[static_cast<size_t>(ld)];
        CHECK(f.count(kEpsilonSymbolId) == 1);   // 因 nullable 含 ε
        CHECK(f.size() == 5);
        CHECK(f.count(idOf(g, "KW_INT", SymbolKind::Terminal)) == 1);
        CHECK(f.count(idOf(g, "KW_FLOAT", SymbolKind::Terminal)) == 1);
        CHECK(f.count(idOf(g, "KW_DOUBLE", SymbolKind::Terminal)) == 1);
        CHECK(f.count(idOf(g, "KW_VOID", SymbolKind::Terminal)) == 1);
    }

    // Follow(statement) ⊇ {KW_ELSE, SYM_RBRACE}
    const int stmt = idOf(g, "statement", SymbolKind::Nonterminal);
    CHECK(stmt >= 0);
    if (stmt >= 0) {
        const std::set<int>& fo = sets.follow[static_cast<size_t>(stmt)];
        CHECK(fo.count(idOf(g, "KW_ELSE", SymbolKind::Terminal)) == 1);
        CHECK(fo.count(idOf(g, "SYM_RBRACE", SymbolKind::Terminal)) == 1);
    }
    return true;
}

// D-10（重点）：firstOfString 的 ε 剥离修正 —— T2.3 核心正确性证据
//   S -> B C ; B -> # | x ; C -> y
//   B 可空、C 不可空 → FIRST(B C) == {x, y}，不得含 ε。
bool testFirstOfStringEpsilonStripping()
{
    auto r = parseText("@start S\nS -> B C ;\nB -> # | x ;\nC -> y ;\n");
    CHECK(r.ok);
    auto fs = FirstFollow().compute(r.value);
    CHECK(fs.ok);
    const auto& g = r.value;
    const auto& sets = fs.value;

    const int b = idOf(g, "B", SymbolKind::Nonterminal);
    const int c = idOf(g, "C", SymbolKind::Nonterminal);
    CHECK(b >= 0 && c >= 0);
    if (b < 0 || c < 0) {
        return false;
    }

    // 核心证据（用户指定断言形式）：
    //   firstOfString({B, C}) == {x, y}
    //   !contains(firstOfString({B, C}), kEpsilonSymbolId)
    const std::set<int> bc = FirstFollow::firstOfString(g, sets, {b, c});
    const std::set<int> xy{ idOf(g, "x", SymbolKind::Terminal),
                            idOf(g, "y", SymbolKind::Terminal) };
    CHECK(bc == xy);
    CHECK(bc.count(kEpsilonSymbolId) == 0);

    // nullable 基础
    CHECK(sets.nullable[static_cast<size_t>(b)]);
    CHECK(!sets.nullable[static_cast<size_t>(c)]);
    CHECK(!sets.nullable[static_cast<size_t>(idOf(g, "S", SymbolKind::Nonterminal))]);

    // FIRST(B) = {#, x}（B 可空 → 含 ε）
    const std::set<int> bset = FirstFollow::firstOfString(g, sets, {b});
    CHECK(bset.count(kEpsilonSymbolId) == 1);
    CHECK(bset.count(idOf(g, "x", SymbolKind::Terminal)) == 1);
    CHECK(bset.size() == 2);

    // FIRST(空串) = {#}
    const std::set<int> eset = FirstFollow::firstOfString(g, sets, {});
    CHECK(eset.size() == 1);
    CHECK(eset.count(kEpsilonSymbolId) == 1);
    return true;
}

// D-7：边界 —— 单产生式 S -> a
bool testBoundarySingleProduction()
{
    auto g = makeHandGrammar(
        { {"S", SymbolKind::Nonterminal}, {"a", SymbolKind::Terminal} },
        { {2, 3} }, 2);
    auto fs = FirstFollow().compute(g);
    CHECK(fs.ok);
    const auto& sets = fs.value;
    CHECK(sets.first[2] == std::set<int>{3});
    CHECK(!sets.nullable[2]);
    CHECK(sets.follow[2] == std::set<int>{0});   // $（id 0）
    CHECK(FirstFollow::firstOfString(g, sets, {2}) == std::set<int>{3});
    return true;
}

// D-8：边界 —— 纯 ε 文法 S -> #
bool testBoundaryEpsilonOnly()
{
    auto g = makeHandGrammar(
        { {"S", SymbolKind::Nonterminal} },
        { {2} }, 2);
    auto fs = FirstFollow().compute(g);
    CHECK(fs.ok);
    const auto& sets = fs.value;
    CHECK(sets.nullable[2]);
    CHECK(sets.first[2] == std::set<int>{kEpsilonSymbolId});
    CHECK(sets.follow[2] == std::set<int>{0});
    return true;
}

// D-9：边界 —— 空文法 / 无效 start → 报错 stage="first-follow"
bool testBoundaryEmptyGrammar()
{
    // 无产生式
    auto g = makeHandGrammar(
        { {"S", SymbolKind::Nonterminal} },
        {}, 2);
    auto fs = FirstFollow().compute(g);
    CHECK(!fs.ok);
    CHECK(hasStageError(fs, "first-follow"));

    // startSymbol 缺失
    auto g2 = makeHandGrammar(
        { {"S", SymbolKind::Nonterminal}, {"a", SymbolKind::Terminal} },
        { {2, 3} }, -1);
    auto fs2 = FirstFollow().compute(g2);
    CHECK(!fs2.ok);
    CHECK(hasStageError(fs2, "first-follow"));
    return true;
}

// D-11：Follow 链传播 —— S->A, A->B, B->C, C->a
bool testFollowChain()
{
    auto r = parseText("@start S\nS -> A ;\nA -> B ;\nB -> C ;\nC -> a ;\n");
    CHECK(r.ok);
    auto fs = FirstFollow().compute(r.value);
    CHECK(fs.ok);
    const auto& g = r.value;
    const auto& sets = fs.value;

    const int s = idOf(g, "S", SymbolKind::Nonterminal);
    const int a = idOf(g, "A", SymbolKind::Nonterminal);
    const int b = idOf(g, "B", SymbolKind::Nonterminal);
    const int c = idOf(g, "C", SymbolKind::Nonterminal);
    CHECK(s >= 0 && a >= 0 && b >= 0 && c >= 0);
    if (s < 0 || a < 0 || b < 0 || c < 0) {
        return false;
    }

    CHECK(sets.follow[static_cast<size_t>(a)] == std::set<int>{0});   // $
    CHECK(sets.follow[static_cast<size_t>(b)] == std::set<int>{0});
    CHECK(sets.follow[static_cast<size_t>(c)] == std::set<int>{0});
    CHECK(sets.first[static_cast<size_t>(s)] ==
          std::set<int>{ idOf(g, "a", SymbolKind::Terminal) });
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
    ok &= testTiny(langRoot);                        // 全链路 + First/Follow/nullable
    ok &= testMinic(langRoot);                       // 全链路 + 5 nullable + First 含 ε
    ok &= testFirstOfStringEpsilonStripping();       // firstOfString 核心正确性（D-10）
    ok &= testBoundarySingleProduction();            // D-7
    ok &= testBoundaryEpsilonOnly();                 // D-8
    ok &= testBoundaryEmptyGrammar();                // D-9
    ok &= testFollowChain();                         // D-11

    if (!ok) {
        std::cerr << "FAIL: 存在未通过的 CHECK（已通过 " << g_checks
                  << " 项检查）" << std::endl;
        return 1;
    }
    std::cout << "first_follow_test OK, checks=" << g_checks << std::endl;
    return 0;
}
