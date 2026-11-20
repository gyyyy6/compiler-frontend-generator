// T2.6 单测：ParseTableBuilder（LALR(1) 分析表 ACTION/GOTO + 冲突报告）。
// 零 Qt、纯 STL；沿用 T1.x/T2.x 冒烟约定：g++ 独立编译运行，退出码 0 = 全部通过。
// 用法：parse_table_test <languages 根目录>（缺省 ./test/languages）
//
// 覆盖（docs/PROGRESS.md 决策点 6 + DATA_STRUCTURES §5.5 + 方案 A-F）：
//   TINY / Mini-C 全量：表行数 == 状态数、GOTO[0][start]==接受态 + 接受态[$]==Accept 不变量、
//   Shift 锚点、TINY 0 冲突、Mini-C 悬垂 else 恰 1 个 S/R（s115/KW_ELSE，reduce 产生式按
//   lhs/rhs 反查不硬编码，表内保留 Shift(118)）；
//   教科书反例 aAd|bBd|aBe|bAe（s6，d/e 各 1 个 R/R，A->c vs B->c）；
//   边界：单产生式 S->a、纯 ε S->#、空文法/无效输入（stage="parse-table"）。
//
// 锚点数字为临时脚本 %TEMP%\t26_derive 推导（2026-10-09 已核对）：
//   TINY：0 冲突、GOTO[0][program]=1（= 接受态）、ACTION[1][$]=Accept、ACTION[0][KW_IF]=Shift(9)；
//   Mini-C：恰 1 个 S/R（s115/KW_ELSE，reduce 产生式 id=31 即 condition-stmt -> KW_IF ( exp ) stmt，
//           ACTION[115][KW_ELSE] 表内保留 Shift(118)）；
//   反例：R/R=2（state=6，terminal=d 与 e，productions={A->c, B->c}）；
//   S->a：3 态 0 冲突（ACTION[0][a]=Shift(2)、ACTION[1][$]=Accept、ACTION[2][$]=Reduce(0)、GOTO[0][S]=1）；
//   S->#：2 态 0 冲突（ACTION[0][$]=Reduce(0)、ACTION[1][$]=Accept、GOTO[0][S]=1）。

#include "BnfParser.h"
#include "FirstFollow.h"
#include "GrammarSimplifier.h"
#include "Lalr1Builder.h"
#include "Lr0Automaton.h"
#include "ParseTable.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using compiler::Symbol;
using compiler::SymbolKind;
using compiler::grammar::BnfParser;
using compiler::grammar::FirstFollow;
using compiler::grammar::GrammarSimplifier;
using compiler::parser::Action;
using compiler::parser::ActionType;
using compiler::parser::Lalr1Builder;
using compiler::parser::Lalr1Data;
using compiler::parser::Lr0Automaton;
using compiler::parser::Lr0AutomatonData;
using compiler::parser::ParseTableBuilder;
using compiler::parser::ParseTableData;

int g_checks = 0;

#define CHECK(...)                                                            \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(__VA_ARGS__)) {                                                 \
            std::cerr << "FAIL @" << __LINE__ << ": " << #__VA_ARGS__ << std::endl; \
            return false;                                                     \
        }                                                                     \
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

// 手工构造 Grammar（满足 BnfParser 不变式：symbols[i].id==i、productions[i].id==i；
// 预置 $=0 / #=1；用户符号自 id 2 起；{lhs} 表示 ε 产生式）。
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
            continue;
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

bool hasStageError(const compiler::Result<ParseTableData>& r, const std::string& stage)
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

// 按 lhs + rhs 精确反查产生式 id（不硬编码；找不到返回 -1）
int findProduction(const compiler::Grammar& g, int lhs, const std::vector<int>& rhs)
{
    for (const auto& p : g.productions) {
        if (p.lhs == lhs && p.rhs == rhs) {
            return p.id;
        }
    }
    return -1;
}

// 查 ACTION 格；越界或缺省返回 nullptr（缺省即 Error）
const Action* findAction(const ParseTableData& d, int state, int terminal)
{
    if (state < 0 || static_cast<size_t>(state) >= d.action.size()) {
        return nullptr;
    }
    const auto& m = d.action[static_cast<size_t>(state)];
    const auto it = m.find(terminal);
    return it == m.end() ? nullptr : &it->second;
}

// 查 GOTO 格；越界或缺省返回 -1
int findGoto(const ParseTableData& d, int state, int symbol)
{
    if (state < 0 || static_cast<size_t>(state) >= d.gotoTable.size()) {
        return -1;
    }
    const auto& m = d.gotoTable[static_cast<size_t>(state)];
    const auto it = m.find(symbol);
    return it == m.end() ? -1 : it->second;
}

// 全链路：First/Follow → LR(0) → LALR(1) → 分析表（返回表 + LALR 中间产物）
bool buildChain(const compiler::Grammar& g, Lalr1Data& lalr,
                compiler::Result<ParseTableData>& table)
{
    auto ff = FirstFollow().compute(g);
    if (!ff.ok) {
        return false;
    }
    auto lr0 = Lr0Automaton().build(g);
    if (!lr0.ok) {
        return false;
    }
    auto lr = Lalr1Builder().build(g, ff.value, lr0.value);
    if (!lr.ok) {
        return false;
    }
    lalr = lr.value;
    table = ParseTableBuilder().build(g, lalr);
    return table.ok;
}

// 不变量：行数 == 状态数；action 键为终结符或 $；goto 键为非终结符且目标在界内
bool checkInvariants(const compiler::Grammar& g, const ParseTableData& t)
{
    CHECK(t.action.size() == t.gotoTable.size());
    for (size_t i = 0; i < t.action.size(); ++i) {
        for (const auto& kv : t.action[i]) {
            const Symbol* s = g.symbol(kv.first);
            CHECK(s && (s->kind == SymbolKind::Terminal ||
                        s->kind == SymbolKind::EndOfFile));
        }
        for (const auto& kv : t.gotoTable[i]) {
            const Symbol* s = g.symbol(kv.first);
            CHECK(s && s->kind == SymbolKind::Nonterminal);
            CHECK(kv.second >= 0 &&
                  static_cast<size_t>(kv.second) < t.gotoTable.size());
        }
    }
    return true;
}

// E-1：TINY 全量（59 态；0 冲突；GOTO[0][program]=1=接受态；ACTION[0][KW_IF]=Shift(9)）
bool testTiny(const std::string& langRoot)
{
    auto r = BnfParser().parseFile(langRoot + "/tiny/tiny.bnf", nullptr);
    CHECK(r.ok);
    auto sg = GrammarSimplifier().simplify(r.value);
    CHECK(sg.ok);
    const auto& g = sg.value.grammar;
    Lalr1Data lalr;
    compiler::Result<ParseTableData> tr;
    CHECK(buildChain(g, lalr, tr));
    const auto& data = tr.value;

    std::cout << "info[tiny] action_rows=" << data.action.size()
              << " goto_rows=" << data.gotoTable.size()
              << " conflicts=" << data.conflicts.size() << std::endl;

    CHECK(data.action.size() == 59);
    CHECK(data.gotoTable.size() == lalr.states.size());
    CHECK(data.conflicts.empty());
    CHECK(checkInvariants(g, data));

    // GOTO[0][program] == 接受态，且接受态 ACTION[$] == Accept
    const int program = idOf(g, "program", SymbolKind::Nonterminal);
    const int kwIf = idOf(g, "KW_IF", SymbolKind::Terminal);
    const auto itg = data.gotoTable[0].find(program);
    CHECK(itg != data.gotoTable[0].end());
    const int accept = itg->second;
    CHECK(accept >= 0 && static_cast<size_t>(accept) < data.action.size());
    const auto ita = data.action[static_cast<size_t>(accept)].find(0);
    CHECK(ita != data.action[static_cast<size_t>(accept)].end());
    CHECK(ita->second.type == ActionType::Accept);

    // Shift 锚点（%TEMP%\t26_derive）：ACTION[0][KW_IF] = Shift(9)
    const auto its = data.action[0].find(kwIf);
    CHECK(its != data.action[0].end());
    CHECK(its->second.type == ActionType::Shift);
    CHECK(its->second.target == 9);

    // GOTO 非空格数 == LALR 非终结符转移数
    int gotoCells = 0, ntTrans = 0;
    for (size_t i = 0; i < data.gotoTable.size(); ++i) {
        gotoCells += static_cast<int>(data.gotoTable[i].size());
    }
    for (const auto& kv : lalr.transitions) {
        const Symbol* s = g.symbol(kv.first.second);
        if (s && s->kind == SymbolKind::Nonterminal) {
            ++ntTrans;
        }
    }
    CHECK(gotoCells == ntTrans);
    return true;
}

// E-2：Mini-C 全量（128 态；恰 1 个 S/R：s115/KW_ELSE/prod31；ACTION[115][KW_ELSE] 保留 Shift(118)）
bool testMinic(const std::string& langRoot)
{
    auto r = BnfParser().parseFile(langRoot + "/minic/minic.bnf", nullptr);
    CHECK(r.ok);
    auto sg = GrammarSimplifier().simplify(r.value);
    CHECK(sg.ok);
    const auto& g = sg.value.grammar;
    Lalr1Data lalr;
    compiler::Result<ParseTableData> tr;
    CHECK(buildChain(g, lalr, tr));
    const auto& data = tr.value;

    std::cout << "info[minic] action_rows=" << data.action.size()
              << " goto_rows=" << data.gotoTable.size()
              << " conflicts=" << data.conflicts.size() << std::endl;

    CHECK(data.action.size() == 128);
    CHECK(data.gotoTable.size() == lalr.states.size());
    CHECK(checkInvariants(g, data));

    // 恰 1 个 S/R 冲突：s115 / KW_ELSE / reduce 产生式按 lhs+rhs 反查
    const int kwElse = idOf(g, "KW_ELSE", SymbolKind::Terminal);
    const int kwIf = idOf(g, "KW_IF", SymbolKind::Terminal);
    const int lp = idOf(g, "SYM_LPAREN", SymbolKind::Terminal);
    const int rp = idOf(g, "SYM_RPAREN", SymbolKind::Terminal);
    const int expr = idOf(g, "expression", SymbolKind::Nonterminal);
    const int stmt = idOf(g, "statement", SymbolKind::Nonterminal);
    const int cond = idOf(g, "condition-stmt", SymbolKind::Nonterminal);
    const int condProd = findProduction(g, cond, {kwIf, lp, expr, rp, stmt});
    CHECK(condProd >= 0);

    CHECK(data.conflicts.size() == 1);
    const auto& c = data.conflicts[0];
    CHECK(c.state == 115);
    CHECK(c.terminal == kwElse);
    CHECK(c.type == "shift/reduce");
    CHECK(c.productions.size() == 1 && c.productions[0] == condProd);

    // 冲突默认策略：表内保留 Shift（标准做法）
    const auto it = data.action[115].find(kwElse);
    CHECK(it != data.action[115].end());
    CHECK(it->second.type == ActionType::Shift);
    CHECK(it->second.target == 118);
    return true;
}

// E-3：教科书反例 S->aAd|bBd|aBe|bAe, A->c, B->c（合并后 R/R=2，均在 s6，d/e，A->c vs B->c）
bool testCounterexample()
{
    // ids: $=0 #=1 S=2 A=3 B=4 a=5 b=6 c=7 d=8 e=9
    const auto g = makeHandGrammar(
        { {"S", SymbolKind::Nonterminal}, {"A", SymbolKind::Nonterminal},
          {"B", SymbolKind::Nonterminal}, {"a", SymbolKind::Terminal},
          {"b", SymbolKind::Terminal}, {"c", SymbolKind::Terminal},
          {"d", SymbolKind::Terminal}, {"e", SymbolKind::Terminal} },
        { {2,5,3,8}, {2,6,4,8}, {2,5,4,9}, {2,6,3,9}, {3,7}, {4,7} }, 2);
    Lalr1Data lalr;
    compiler::Result<ParseTableData> tr;
    CHECK(buildChain(g, lalr, tr));
    const auto& data = tr.value;

    const int dId = idOf(g, "d", SymbolKind::Terminal);
    const int eId = idOf(g, "e", SymbolKind::Terminal);
    const int aToC = findProduction(g, 3, {7});
    const int bToC = findProduction(g, 4, {7});
    CHECK(aToC >= 0 && bToC >= 0);

    CHECK(data.conflicts.size() == 2);
    std::set<int> conflictTerms;
    for (const auto& c : data.conflicts) {
        CHECK(c.state == 6);
        CHECK(c.type == "reduce/reduce");
        CHECK(c.productions.size() == 2);
        CHECK(c.productions[0] == aToC && c.productions[1] == bToC);
        conflictTerms.insert(c.terminal);
    }
    CHECK(conflictTerms == std::set<int>({dId, eId}));
    return true;
}

// E-4：单产生式 S->a（3 态 0 冲突：ACTION[0][a]=Shift(2)、ACTION[1][$]=Accept、
//      ACTION[2][$]=Reduce(0)、GOTO[0][S]=1）
bool testSingleProduction()
{
    const auto g = makeHandGrammar(
        { {"S", SymbolKind::Nonterminal}, {"a", SymbolKind::Terminal} },
        { {2, 3} }, 2);
    Lalr1Data lalr;
    compiler::Result<ParseTableData> tr;
    CHECK(buildChain(g, lalr, tr));
    const auto& data = tr.value;

    CHECK(data.action.size() == 3);
    CHECK(data.conflicts.empty());
    CHECK(checkInvariants(g, data));

    const int aId = idOf(g, "a", SymbolKind::Terminal);
    const int sId = idOf(g, "S", SymbolKind::Nonterminal);
    const Action* a0 = findAction(data, 0, aId);
    const Action* a1 = findAction(data, 1, 0);
    const Action* a2 = findAction(data, 2, 0);
    CHECK(a0 && a0->type == ActionType::Shift && a0->target == 2);
    CHECK(a1 && a1->type == ActionType::Accept);
    CHECK(a2 && a2->type == ActionType::Reduce && a2->target == 0);
    CHECK(findGoto(data, 0, sId) == 1);
    return true;
}

// E-5：纯 ε S->#（2 态 0 冲突：ACTION[0][$]=Reduce(0)、ACTION[1][$]=Accept、GOTO[0][S]=1）
bool testEpsilonOnly()
{
    const auto g = makeHandGrammar(
        { {"S", SymbolKind::Nonterminal} },
        { {2} }, 2);
    Lalr1Data lalr;
    compiler::Result<ParseTableData> tr;
    CHECK(buildChain(g, lalr, tr));
    const auto& data = tr.value;

    CHECK(data.action.size() == 2);
    CHECK(data.conflicts.empty());
    CHECK(checkInvariants(g, data));

    const int sId = idOf(g, "S", SymbolKind::Nonterminal);
    const Action* a0 = findAction(data, 0, 0);
    const Action* a1 = findAction(data, 1, 0);
    CHECK(a0 && a0->type == ActionType::Reduce && a0->target == 0);
    CHECK(a1 && a1->type == ActionType::Accept);
    CHECK(findGoto(data, 0, sId) == 1);
    return true;
}

// E-6：边界 —— 错误路径 stage="parse-table"
bool testInvalid()
{
    const auto g = makeHandGrammar(
        { {"S", SymbolKind::Nonterminal}, {"a", SymbolKind::Terminal} },
        { {2, 3} }, 2);
    auto ff = FirstFollow().compute(g);
    CHECK(ff.ok);
    auto lr0 = Lr0Automaton().build(g);
    CHECK(lr0.ok);
    auto lr = Lalr1Builder().build(g, ff.value, lr0.value);
    CHECK(lr.ok);

    // 产生式为空
    auto gEmpty = g;
    gEmpty.productions.clear();
    auto r1 = ParseTableBuilder().build(gEmpty, lr.value);
    CHECK(!r1.ok);
    CHECK(hasStageError(r1, "parse-table"));

    // startSymbol 无效
    auto gBad = g;
    gBad.startSymbol = -1;
    auto r2 = ParseTableBuilder().build(gBad, lr.value);
    CHECK(!r2.ok);
    CHECK(hasStageError(r2, "parse-table"));

    // Lalr1Data 为空
    Lalr1Data empty;
    auto r3 = ParseTableBuilder().build(g, empty);
    CHECK(!r3.ok);
    CHECK(hasStageError(r3, "parse-table"));

    // 增广信息与 Grammar 不一致（rhs 非 {start}）
    auto badAug = lr.value;
    badAug.augmentedProductionRhs.clear();
    auto r4 = ParseTableBuilder().build(g, badAug);
    CHECK(!r4.ok);
    CHECK(hasStageError(r4, "parse-table"));
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
    ok &= testCounterexample();
    ok &= testSingleProduction();
    ok &= testEpsilonOnly();
    ok &= testInvalid();

    if (!ok) {
        std::cerr << "FAIL: 存在未通过的 CHECK（已通过 " << g_checks
                  << " 项检查）" << std::endl;
        return 1;
    }
    std::cout << "parse_table_test OK, checks=" << g_checks << std::endl;
    return 0;
}
