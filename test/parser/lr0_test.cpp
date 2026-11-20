// T2.4 单测：Lr0Automaton（LR(0) 项目集族：closure / goto / BFS + 增广 S'）。
// 零 Qt、纯 STL；沿用 T1.x/T2.x 冒烟约定：g++ 独立编译运行，退出码 0 = 全部通过。
// 用法：lr0_test <languages 根目录>（缺省 ./test/languages）
//
// 覆盖（docs/PROGRESS.md 决策点 + 方案 E）：
//   TINY / Mini-C 全量（BnfParser→Simplifier→Lr0Automaton）：状态数范围护栏、初始状态、
//   goto 锚点、接受态、closure 完备不变量、转移合法；
//   边界：单产生式（3 态）、纯 ε（2 态）、空文法 / 无效 start（报错 stage="lr0-build"）。
//
// 状态数（见 DATA_STRUCTURES §5.3）：TINY=59、Mini-C=128（推导值，C++ 实现已复核一致，
// 测试断言精确值）。

#include "BnfParser.h"
#include "GrammarSimplifier.h"
#include "Lr0Automaton.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using compiler::SymbolKind;
using compiler::grammar::BnfParser;
using compiler::grammar::GrammarSimplifier;
using compiler::parser::Item;
using compiler::parser::Lr0Automaton;
using compiler::parser::Lr0AutomatonData;

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

// 手工构造 Grammar（满足 BnfParser 不变式：symbols[i].id==i、productions[i].id==i；
// 预置 $=0 / #=1；用户符号自 id 2 起）。
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

bool hasStageError(const compiler::Result<Lr0AutomatonData>& r,
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

// 产生式右部路由（与 Lr0Automaton 内部一致：增广 id → 增广右部）
const std::vector<int>& rhsOf(const compiler::Grammar& g,
                              const Lr0AutomatonData& data, int pid)
{
    if (pid == data.augmentedProductionId) {
        return data.augmentedProductionRhs;
    }
    return g.productions[static_cast<size_t>(pid)].rhs;
}

int findProd(const compiler::Grammar& g, int lhs, const std::vector<int>& rhs)
{
    for (const auto& p : g.productions) {
        if (p.lhs == lhs && p.rhs == rhs) {
            return p.id;
        }
    }
    return -1;
}

// 状态 0 的出边符号集合
std::set<int> outSymsOf0(const compiler::Grammar& g, const Lr0AutomatonData& data)
{
    std::set<int> out;
    for (const Item& it : data.states[0].items) {
        const std::vector<int>& r = rhsOf(g, data, it.productionId);
        if (it.dot >= 0 && static_cast<size_t>(it.dot) < r.size()) {
            out.insert(r[static_cast<size_t>(it.dot)]);
        }
    }
    return out;
}

// 不变量：id 连续、状态非空、closure 完备、转移目标/符号 id 合法
bool checkInvariants(const compiler::Grammar& g, const Lr0AutomatonData& data)
{
    for (size_t i = 0; i < data.states.size(); ++i) {
        CHECK(data.states[i].id == static_cast<int>(i));
        CHECK(!data.states[i].items.empty());
    }
    // closure 完备：点后为非终结符 B 的项，B 的全部产生式 dot=0 项都在本状态
    for (const auto& st : data.states) {
        for (const Item& it : st.items) {
            const std::vector<int>& r = rhsOf(g, data, it.productionId);
            if (it.dot >= 0 && static_cast<size_t>(it.dot) < r.size()) {
                const int B = r[static_cast<size_t>(it.dot)];
                const compiler::Symbol* sym = g.symbol(B);
                if (sym && sym->kind == SymbolKind::Nonterminal) {
                    for (int pid : g.productionsOf(B)) {
                        CHECK(st.items.count(Item{ pid, 0 }) == 1);
                    }
                }
            }
        }
    }
    // 转移表：源/目标状态 id 合法、符号 id 合法
    for (const auto& kv : data.transitions) {
        CHECK(kv.first.first >= 0 &&
              kv.first.first < static_cast<int>(data.states.size()));
        CHECK(kv.first.second >= 0 &&
              kv.first.second < static_cast<int>(g.symbols.size()));
        CHECK(kv.second >= 0 && kv.second < static_cast<int>(data.states.size()));
    }
    return true;
}

// 接受态：含 [S' -> S ·]，且恰好只有这一项
bool checkAccept(const Lr0AutomatonData& data)
{
    const Item acc{ data.augmentedProductionId, 1 };
    bool found = false;
    for (const auto& st : data.states) {
        if (st.items.count(acc) == 1) {
            found = true;
            CHECK(st.items.size() == 1);
        }
    }
    CHECK(found);
    return true;
}

// E-1/2/3：TINY 全量
bool testTiny(const std::string& langRoot)
{
    auto r = BnfParser().parseFile(langRoot + "/tiny/tiny.bnf", nullptr);
    CHECK(r.ok);
    auto sg = GrammarSimplifier().simplify(r.value);
    CHECK(sg.ok);
    auto lr = Lr0Automaton().build(sg.value.grammar);
    CHECK(lr.ok);
    const auto& g = sg.value.grammar;
    const auto& data = lr.value;

    std::cout << "info[tiny] states=" << data.states.size()
              << " transitions=" << data.transitions.size() << std::endl;

    // 状态数精确断言（推导值 59，实现已复核一致；见 DATA_STRUCTURES §5.3）
    CHECK(data.states.size() == 59);
    CHECK(!data.transitions.empty());

    // 增广文法
    CHECK(data.augmentedStartSymbolName == "S'");
    CHECK(data.augmentedStartSymbolId == static_cast<int>(g.symbols.size()));
    CHECK(data.augmentedProductionId == static_cast<int>(g.productions.size()));
    CHECK(data.augmentedProductionRhs.size() == 1);
    if (!data.augmentedProductionRhs.empty()) {
        CHECK(data.augmentedProductionRhs[0] == g.startSymbol);
    }

    // 初始状态：含 [S' -> ·program] 与全部起始非终结符产生式
    const auto& s0 = data.states[0].items;
    CHECK(s0.count(Item{ data.augmentedProductionId, 0 }) == 1);
    for (int pid : g.productionsOf(g.startSymbol)) {
        CHECK(s0.count(Item{ pid, 0 }) == 1);
    }
    const int stmtSeq = idOf(g, "stmt-sequence", SymbolKind::Nonterminal);
    const int stmt = idOf(g, "statement", SymbolKind::Nonterminal);
    for (int pid : g.productionsOf(stmtSeq)) {
        CHECK(s0.count(Item{ pid, 0 }) == 1);
    }
    for (int pid : g.productionsOf(stmt)) {
        CHECK(s0.count(Item{ pid, 0 }) == 1);
    }
    CHECK(s0.size() == 15);   // 推导值（含增广项）；实现后复核
    CHECK(outSymsOf0(g, data).size() == 13);   // 推导值；实现后复核

    // goto(state0, KW_IF) → 含 [if-stmt -> KW_IF · exp ...] 与 exp closure 链
    const int kwIf = idOf(g, "KW_IF", SymbolKind::Terminal);
    const auto it = data.transitions.find({ 0, kwIf });
    CHECK(it != data.transitions.end());
    if (it != data.transitions.end()) {
        const auto& st = data.states[static_cast<size_t>(it->second)].items;
        const auto ifProds = g.productionsOf(idOf(g, "if-stmt", SymbolKind::Nonterminal));
        CHECK(ifProds.size() == 2);
        for (int pid : ifProds) {
            CHECK(st.count(Item{ pid, 1 }) == 1);   // dot 越过 KW_IF
        }
        const int exp = idOf(g, "exp", SymbolKind::Nonterminal);
        for (int pid : g.productionsOf(exp)) {
            CHECK(st.count(Item{ pid, 0 }) == 1);
        }
        CHECK(st.size() == 13);   // 2 个 if-stmt dot=1 + exp 链 11 项；实现后复核
    }

    CHECK(checkAccept(data));
    CHECK(checkInvariants(g, data));
    return true;
}

// E-4/5：Mini-C 全量
bool testMinic(const std::string& langRoot)
{
    auto r = BnfParser().parseFile(langRoot + "/minic/minic.bnf", nullptr);
    CHECK(r.ok);
    auto sg = GrammarSimplifier().simplify(r.value);
    CHECK(sg.ok);
    auto lr = Lr0Automaton().build(sg.value.grammar);
    CHECK(lr.ok);
    const auto& g = sg.value.grammar;
    const auto& data = lr.value;

    std::cout << "info[minic] states=" << data.states.size()
              << " transitions=" << data.transitions.size() << std::endl;

    // 状态数精确断言（推导值 128，实现已复核一致；见 DATA_STRUCTURES §5.3）
    CHECK(data.states.size() == 128);
    CHECK(!data.transitions.empty());

    // 初始状态：含 [S' -> ·program] 与 program / definition-list 产生式
    const auto& s0 = data.states[0].items;
    CHECK(s0.count(Item{ data.augmentedProductionId, 0 }) == 1);
    for (int pid : g.productionsOf(g.startSymbol)) {
        CHECK(s0.count(Item{ pid, 0 }) == 1);
    }
    const int defList = idOf(g, "definition-list", SymbolKind::Nonterminal);
    for (int pid : g.productionsOf(defList)) {
        CHECK(s0.count(Item{ pid, 0 }) == 1);
    }
    CHECK(s0.size() == 13);   // 推导值（含增广项）；实现后复核
    CHECK(outSymsOf0(g, data).size() == 10);   // 推导值；实现后复核

    // goto(state0, KW_INT) → 含完成项 [type-indicator -> KW_INT ·]
    const int kwInt = idOf(g, "KW_INT", SymbolKind::Terminal);
    const int ti = idOf(g, "type-indicator", SymbolKind::Nonterminal);
    const int tiInt = findProd(g, ti, { kwInt });
    CHECK(tiInt >= 0);
    const auto it = data.transitions.find({ 0, kwInt });
    CHECK(it != data.transitions.end());
    if (it != data.transitions.end() && tiInt >= 0) {
        const auto& st = data.states[static_cast<size_t>(it->second)].items;
        CHECK(st.count(Item{ tiInt, 1 }) == 1);   // [type-indicator -> KW_INT ·] 完成项
    }

    CHECK(checkAccept(data));
    CHECK(checkInvariants(g, data));
    return true;
}

// E-7：边界 —— 单产生式 S -> a（3 态）
bool testSingleProduction()
{
    auto g = makeHandGrammar(
        { {"S", SymbolKind::Nonterminal}, {"a", SymbolKind::Terminal} },
        { {2, 3} }, 2);
    auto lr = Lr0Automaton().build(g);
    CHECK(lr.ok);
    CHECK(lr.value.states.size() == 3);
    CHECK(checkAccept(lr.value));
    CHECK(checkInvariants(g, lr.value));
    return true;
}

// E-8：边界 —— 纯 ε 文法 S -> #（2 态）
bool testEpsilonOnly()
{
    auto g = makeHandGrammar(
        { {"S", SymbolKind::Nonterminal} },
        { {2} }, 2);
    auto lr = Lr0Automaton().build(g);
    CHECK(lr.ok);
    CHECK(lr.value.states.size() == 2);
    // 初始状态含完成项 [S -> ·]
    CHECK(lr.value.states[0].items.count(Item{ 0, 0 }) == 1);
    CHECK(checkAccept(lr.value));
    return true;
}

// E-9：边界 —— 空文法 / 无效 start → 报错 stage="lr0-build"
bool testInvalidGrammar()
{
    auto g = makeHandGrammar(
        { {"S", SymbolKind::Nonterminal} },
        {}, 2);
    auto lr = Lr0Automaton().build(g);
    CHECK(!lr.ok);
    CHECK(hasStageError(lr, "lr0-build"));

    auto g2 = makeHandGrammar(
        { {"S", SymbolKind::Nonterminal}, {"a", SymbolKind::Terminal} },
        { {2, 3} }, -1);
    auto lr2 = Lr0Automaton().build(g2);
    CHECK(!lr2.ok);
    CHECK(hasStageError(lr2, "lr0-build"));
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
    ok &= testSingleProduction();
    ok &= testEpsilonOnly();
    ok &= testInvalidGrammar();

    if (!ok) {
        std::cerr << "FAIL: 存在未通过的 CHECK（已通过 " << g_checks
                  << " 项检查）" << std::endl;
        return 1;
    }
    std::cout << "lr0_test OK, checks=" << g_checks << std::endl;
    return 0;
}
