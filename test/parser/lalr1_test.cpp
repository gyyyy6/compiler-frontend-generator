// T2.5 单测：Lalr1Builder（LALR(1) 项目集族：LR(1) + 同心合并）。
// 零 Qt、纯 STL；沿用 T1.x/T2.x 冒烟约定：g++ 独立编译运行，退出码 0 = 全部通过。
// 用法：lalr1_test <languages 根目录>（缺省 ./test/languages）
//
// 覆盖（docs/PROGRESS.md 决策点 1 路线 B + DATA_STRUCTURES §5.4 + 方案 F）：
//   TINY / Mini-C 全量：状态数精确值 + 范围护栏、转移数 == LR(0)、状态 0 / 接受态 /
//   特定状态 lookahead 锚点、冲突计数（LR(1) vs 合并后，断言「合并不引入新 S/R」）；
//   经典正例 L=R（lookahead 分裂 → 合并回 LR(0)）；教科书反例 aAd|bBd|aBe|bAe
//   （合并引入 R/R=2 且 S/R 不新增）；边界：平衡括号 / 单产生式 / 纯 ε / 错误路径。
//
// 锚点数字为临时脚本 %TEMP%\t25_derive 推导（2026-10-08 已核对）：
//   TINY 59→309→59（转移 158）、Mini-C 128→408→128（转移 441）、
//   Mini-C 悬垂 else sr=1（LR(1) s397 / 合并 s115，on KW_ELSE）、
//   L=R：10→14→10（0/0）、aAd|bBd|aBe|bAe：13→14→13（LR(1) 0/0 → 合并 rr=2，s6）。

#include "BnfParser.h"
#include "FirstFollow.h"
#include "GrammarSimplifier.h"
#include "Lalr1Builder.h"
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

using compiler::Symbol;
using compiler::SymbolKind;
using compiler::grammar::BnfParser;
using compiler::grammar::FirstFollow;
using compiler::grammar::FirstFollowSets;
using compiler::grammar::GrammarSimplifier;
using compiler::parser::Lalr1Builder;
using compiler::parser::Lalr1Data;
using compiler::parser::Lalr1Item;
using compiler::parser::Lalr1State;
using compiler::parser::Lr1Item;
using compiler::parser::Lr1StateRaw;
using compiler::parser::Lr0Automaton;
using compiler::parser::Lr0AutomatonData;

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

bool hasStageError(const compiler::Result<Lalr1Data>& r, const std::string& stage)
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

// 产生式右部路由（与构建器内部一致：增广 id → 增广右部）
const std::vector<int>& rhsOf(const compiler::Grammar& g,
                              const Lr0AutomatonData& lr0, int pid)
{
    if (pid == lr0.augmentedProductionId) {
        return lr0.augmentedProductionRhs;
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

// 在合并态中按 (pid, dot) 查找项；找不到返回 nullptr
const Lalr1Item* findItem(const Lalr1State& st, int pid, int dot)
{
    for (const Lalr1Item& it : st.items) {
        if (it.productionId == pid && it.dot == dot) {
            return &it;
        }
    }
    return nullptr;
}

bool isShiftTerminal(const compiler::Grammar& g, int sym)
{
    const Symbol* s = g.symbol(sym);
    return s && s->kind == SymbolKind::Terminal;
}

// 合并态扁平化 → 单 lookahead 表示（与 LR(1) 中间态同构，复用同一冲突计数）
std::vector<Lr1StateRaw> flattenMerged(const std::vector<Lalr1State>& states)
{
    std::vector<Lr1StateRaw> out;
    out.reserve(states.size());
    for (const Lalr1State& st : states) {
        Lr1StateRaw raw;
        raw.id = st.id;
        for (const Lalr1Item& it : st.items) {
            for (int a : it.lookaheads) {
                raw.items.insert(Lr1Item{ it.productionId, it.dot, a });
            }
        }
        out.push_back(std::move(raw));
    }
    return out;
}

struct ConflictCount {
    int shiftReduce = 0;    // 某终结符既是 shift 又在某完成项 lookahead
    int reduceReduce = 0;   // 两个不同完成项共享某 lookahead
};

ConflictCount countConflicts(const compiler::Grammar& g, const Lr0AutomatonData& lr0,
                             const std::vector<Lr1StateRaw>& states,
                             const std::map<std::pair<int, int>, int>& transitions)
{
    ConflictCount c;
    for (const Lr1StateRaw& st : states) {
        std::set<int> shiftTerms;
        for (const auto& kv : transitions) {
            if (kv.first.first == st.id && isShiftTerminal(g, kv.first.second)) {
                shiftTerms.insert(kv.first.second);
            }
        }
        std::map<std::pair<int, int>, std::set<int>> red;   // (pid,dot) -> lookahead 集合
        for (const Lr1Item& it : st.items) {
            const std::vector<int>& r = rhsOf(g, lr0, it.productionId);
            if (it.dot >= 0 && static_cast<size_t>(it.dot) == r.size()) {
                red[{ it.productionId, it.dot }].insert(it.lookahead);
            }
        }
        std::map<int, std::set<std::pair<int, int>>> laOwners;
        for (const auto& kv : red) {
            for (int a : kv.second) {
                laOwners[a].insert(kv.first);
            }
        }
        for (const auto& kv : laOwners) {
            if (shiftTerms.count(kv.first) != 0) {
                ++c.shiftReduce;
            }
            if (kv.second.size() >= 2) {
                ++c.reduceReduce;
            }
        }
    }
    return c;
}

// 接受态：恰含一个 [S' -> S·]，lookahead 恒为 {$}
bool checkAccept(const Lalr1Data& data)
{
    int found = 0;
    for (const Lalr1State& st : data.states) {
        for (const Lalr1Item& it : st.items) {
            if (it.productionId == data.augmentedProductionId && it.dot == 1) {
                ++found;
                CHECK(it.lookaheads == std::set<int>{0});
            }
        }
    }
    CHECK(found == 1);
    return true;
}

// 不变量：id 连续、状态非空、项/转移合法、lookahead 均为终结符或 $
bool checkInvariants(const compiler::Grammar& g, const Lr0AutomatonData& lr0,
                     const Lalr1Data& data)
{
    CHECK(data.states.size() == lr0.states.size());
    for (size_t i = 0; i < data.states.size(); ++i) {
        CHECK(data.states[i].id == static_cast<int>(i));
        CHECK(!data.states[i].items.empty());
        for (const Lalr1Item& it : data.states[i].items) {
            const std::vector<int>& r = rhsOf(g, lr0, it.productionId);
            CHECK(it.dot >= 0 && static_cast<size_t>(it.dot) <= r.size());
            CHECK(!it.lookaheads.empty());
            for (int a : it.lookaheads) {
                const Symbol* s = g.symbol(a);
                CHECK(s && (s->kind == SymbolKind::Terminal ||
                            s->kind == SymbolKind::EndOfFile));
            }
        }
    }
    for (const auto& kv : data.transitions) {
        CHECK(kv.first.first >= 0 &&
              static_cast<size_t>(kv.first.first) < data.states.size());
        CHECK(g.symbol(kv.first.second) != nullptr);
        CHECK(kv.second >= 0 && static_cast<size_t>(kv.second) < data.states.size());
    }
    CHECK(!data.intermediateLr1States.empty());
    for (size_t i = 0; i < data.intermediateLr1States.size(); ++i) {
        CHECK(data.intermediateLr1States[i].id == static_cast<int>(i));
        CHECK(!data.intermediateLr1States[i].items.empty());
    }
    return true;
}

// E-1：TINY 全量（59→309→59；0/0 冲突；状态 0 / 接受态 / s3 / s30 lookahead 锚点）
bool testTiny(const std::string& langRoot)
{
    auto r = BnfParser().parseFile(langRoot + "/tiny/tiny.bnf", nullptr);
    CHECK(r.ok);
    auto sg = GrammarSimplifier().simplify(r.value);
    CHECK(sg.ok);
    auto ff = FirstFollow().compute(sg.value.grammar);
    CHECK(ff.ok);
    auto lr0 = Lr0Automaton().build(sg.value.grammar);
    CHECK(lr0.ok);
    auto lr = Lalr1Builder().build(sg.value.grammar, ff.value, lr0.value);
    CHECK(lr.ok);
    const auto& g = sg.value.grammar;
    const auto& data = lr.value;

    std::cout << "info[tiny] lr1=" << data.intermediateLr1States.size()
              << " merged=" << data.states.size()
              << " lr1Trans=" << data.intermediateLr1Transitions.size()
              << " mergedTrans=" << data.transitions.size() << std::endl;

    // 状态数：精确值 + 范围护栏（DATA_STRUCTURES §5.4）
    CHECK(data.intermediateLr1States.size() == 309);
    CHECK(data.intermediateLr1States.size() >= 150 &&
          data.intermediateLr1States.size() <= 500);
    CHECK(data.states.size() == 59);
    CHECK(data.states.size() == lr0.value.states.size());
    CHECK(data.transitions.size() == lr0.value.transitions.size());
    CHECK(data.transitions.size() == 158);

    // 冲突：合并前后均为 0/0；关键性质「合并不引入新 S/R」成立
    const auto confLr1 = countConflicts(g, lr0.value, data.intermediateLr1States,
                                        data.intermediateLr1Transitions);
    const auto confMerged = countConflicts(g, lr0.value, flattenMerged(data.states),
                                           data.transitions);
    CHECK(confLr1.shiftReduce == 0 && confLr1.reduceReduce == 0);
    CHECK(confMerged.shiftReduce == confLr1.shiftReduce);
    CHECK(confMerged.reduceReduce == confLr1.reduceReduce);

    // 状态 0 lookahead 锚点
    const int program = idOf(g, "program", SymbolKind::Nonterminal);
    const int stmtSeq = idOf(g, "stmt-sequence", SymbolKind::Nonterminal);
    const int statement = idOf(g, "statement", SymbolKind::Nonterminal);
    const int semi = idOf(g, "SYM_SEMI", SymbolKind::Terminal);
    const int kwEnd = idOf(g, "KW_END", SymbolKind::Terminal);
    const int kwElse = idOf(g, "KW_ELSE", SymbolKind::Terminal);
    const int kwUntil = idOf(g, "KW_UNTIL", SymbolKind::Terminal);
    CHECK(program >= 0 && stmtSeq >= 0 && statement >= 0 && semi >= 0);

    const auto& s0 = data.states[0];
    const Lalr1Item* aug0 = findItem(s0, data.augmentedProductionId, 0);
    CHECK(aug0 != nullptr);
    if (aug0) {
        CHECK(aug0->lookaheads == std::set<int>{0});   // [S' -> ·program] LA={$}
    }
    const int programProd = findProd(g, program, { stmtSeq });
    CHECK(programProd >= 0);
    const Lalr1Item* p0 = findItem(s0, programProd, 0);
    CHECK(p0 != nullptr);
    if (p0) {
        CHECK(p0->lookaheads == std::set<int>{0});     // [program -> ·stmt-sequence] LA={$}
    }
    const int ssProd = findProd(g, stmtSeq, { stmtSeq, semi, statement });
    CHECK(ssProd >= 0);
    const Lalr1Item* ss0 = findItem(s0, ssProd, 0);
    CHECK(ss0 != nullptr);
    if (ss0) {
        CHECK(ss0->lookaheads == std::set<int>{0, semi});   // LA={$, SYM_SEMI}
    }

    // 接受态 id=1（推导锚点），lookahead 恒 {$}
    CHECK(checkAccept(data));
    CHECK(data.states[1].items.count(
              Lalr1Item{ data.augmentedProductionId, 1, std::set<int>{0} }) == 1);

    // s3：[stmt-sequence -> statement ·] LA={$, SYM_SEMI, KW_END, KW_ELSE, KW_UNTIL}
    const int stmtProd = findProd(g, stmtSeq, { statement });
    CHECK(stmtProd >= 0);
    const Lalr1Item* s3 = findItem(data.states[3], stmtProd, 1);
    CHECK(s3 != nullptr);
    if (s3) {
        CHECK(s3->lookaheads == std::set<int>{0, semi, kwEnd, kwElse, kwUntil});
    }

    // s30：[comparison-op -> SYM_LT ·] LA={ID, SYM_LPAREN, NUM}（非平凡锚点）
    const int cmpOp = idOf(g, "comparison-op", SymbolKind::Nonterminal);
    const int symLt = idOf(g, "SYM_LT", SymbolKind::Terminal);
    const int idT = idOf(g, "ID", SymbolKind::Terminal);
    const int lparen = idOf(g, "SYM_LPAREN", SymbolKind::Terminal);
    const int num = idOf(g, "NUM", SymbolKind::Terminal);
    CHECK(cmpOp >= 0 && symLt >= 0 && idT >= 0 && lparen >= 0 && num >= 0);
    const int cmpLt = findProd(g, cmpOp, { symLt });
    CHECK(cmpLt >= 0);
    const Lalr1Item* s30 = findItem(data.states[30], cmpLt, 1);
    CHECK(s30 != nullptr);
    if (s30) {
        CHECK(s30->lookaheads == std::set<int>{ idT, lparen, num });
    }

    CHECK(checkInvariants(g, lr0.value, data));
    return true;
}

// E-2：Mini-C 全量（128→408→128；悬垂 else sr=1 按推导锚点断言，合并不新增）
bool testMinic(const std::string& langRoot)
{
    auto r = BnfParser().parseFile(langRoot + "/minic/minic.bnf", nullptr);
    CHECK(r.ok);
    auto sg = GrammarSimplifier().simplify(r.value);
    CHECK(sg.ok);
    auto ff = FirstFollow().compute(sg.value.grammar);
    CHECK(ff.ok);
    auto lr0 = Lr0Automaton().build(sg.value.grammar);
    CHECK(lr0.ok);
    auto lr = Lalr1Builder().build(sg.value.grammar, ff.value, lr0.value);
    CHECK(lr.ok);
    const auto& g = sg.value.grammar;
    const auto& data = lr.value;

    std::cout << "info[minic] lr1=" << data.intermediateLr1States.size()
              << " merged=" << data.states.size()
              << " lr1Trans=" << data.intermediateLr1Transitions.size()
              << " mergedTrans=" << data.transitions.size() << std::endl;

    // 状态数：精确值 + 范围护栏
    CHECK(data.intermediateLr1States.size() == 408);
    CHECK(data.intermediateLr1States.size() >= 300 &&
          data.intermediateLr1States.size() <= 700);
    CHECK(data.states.size() == 128);
    CHECK(data.states.size() == lr0.value.states.size());
    CHECK(data.transitions.size() == lr0.value.transitions.size());
    CHECK(data.transitions.size() == 441);

    // 冲突：LR(1) 与合并后均为 sr=1 rr=0（悬垂 else）；合并不引入新 S/R
    const auto confLr1 = countConflicts(g, lr0.value, data.intermediateLr1States,
                                        data.intermediateLr1Transitions);
    const auto confMerged = countConflicts(g, lr0.value, flattenMerged(data.states),
                                           data.transitions);
    CHECK(confLr1.shiftReduce == 1 && confLr1.reduceReduce == 0);
    CHECK(confMerged.shiftReduce == confLr1.shiftReduce);
    CHECK(confMerged.reduceReduce == confLr1.reduceReduce);

    // 状态 0 lookahead 锚点
    const int program = idOf(g, "program", SymbolKind::Nonterminal);
    const int defList = idOf(g, "definition-list", SymbolKind::Nonterminal);
    const int ti = idOf(g, "type-indicator", SymbolKind::Nonterminal);
    const int kwInt = idOf(g, "KW_INT", SymbolKind::Terminal);
    const int idT = idOf(g, "ID", SymbolKind::Terminal);
    CHECK(program >= 0 && defList >= 0 && ti >= 0 && kwInt >= 0 && idT >= 0);

    const auto& s0 = data.states[0];
    const Lalr1Item* aug0 = findItem(s0, data.augmentedProductionId, 0);
    CHECK(aug0 != nullptr);
    if (aug0) {
        CHECK(aug0->lookaheads == std::set<int>{0});   // [S' -> ·program] LA={$}
    }
    const int progProd = findProd(g, program, { defList });
    CHECK(progProd >= 0);
    const Lalr1Item* pg0 = findItem(s0, progProd, 0);
    CHECK(pg0 != nullptr);
    if (pg0) {
        CHECK(pg0->lookaheads == std::set<int>{0});    // [program -> ·definition-list] LA={$}
    }
    const int tiInt = findProd(g, ti, { kwInt });
    CHECK(tiInt >= 0);
    const Lalr1Item* ti0 = findItem(s0, tiInt, 0);
    CHECK(ti0 != nullptr);
    if (ti0) {
        CHECK(ti0->lookaheads == std::set<int>{ idT });  // [type-indicator -> ·KW_INT] LA={ID}
    }

    // 接受态 id=1，lookahead 恒 {$}
    CHECK(checkAccept(data));
    CHECK(data.states[1].items.count(
              Lalr1Item{ data.augmentedProductionId, 1, std::set<int>{0} }) == 1);

    // 悬垂 else：合并 s115（推导锚点）——shift KW_ELSE→118 + 完成项含 KW_ELSE lookahead
    const int kwElse = idOf(g, "KW_ELSE", SymbolKind::Terminal);
    const int lparen = idOf(g, "SYM_LPAREN", SymbolKind::Terminal);
    const int rparen = idOf(g, "SYM_RPAREN", SymbolKind::Terminal);
    const int expression = idOf(g, "expression", SymbolKind::Nonterminal);
    const int statement = idOf(g, "statement", SymbolKind::Nonterminal);
    const int condStmt = idOf(g, "condition-stmt", SymbolKind::Nonterminal);
    CHECK(kwElse >= 0 && lparen >= 0 && rparen >= 0 && expression >= 0 &&
          statement >= 0 && condStmt >= 0);
    // condition-stmt -> KW_IF ( expression ) statement（5 个右部符号，完成项 dot=5）
    const int condIfProd = findProd(g, condStmt,
                                    { idOf(g, "KW_IF", SymbolKind::Terminal),
                                      lparen, expression, rparen, statement });
    CHECK(condIfProd >= 0);
    CHECK(data.transitions.count({ 115, kwElse }) == 1);
    if (data.transitions.count({ 115, kwElse }) == 1) {
        CHECK(data.transitions.at({ 115, kwElse }) == 118);
    }
    const Lalr1Item* red115 = findItem(data.states[115], condIfProd, 5);
    CHECK(red115 != nullptr);
    if (red115) {
        CHECK(red115->lookaheads.count(kwElse) == 1);
    }

    // LR(1) 冲突态 s397（推导锚点）：shift KW_ELSE + 完成项 lookahead KW_ELSE
    CHECK(data.intermediateLr1States.size() > 397);
    CHECK(data.intermediateLr1States[397].id == 397);
    CHECK(data.intermediateLr1Transitions.count({ 397, kwElse }) == 1);
    bool lr1CondReduce = false;
    for (const Lr1Item& it : data.intermediateLr1States[397].items) {
        if (it.productionId == condIfProd && it.dot == 5 && it.lookahead == kwElse) {
            lr1CondReduce = true;
        }
    }
    CHECK(lr1CondReduce);

    CHECK(checkInvariants(g, lr0.value, data));
    return true;
}

// E-3：经典正例 S->L=R|R, L->*R|id, R->L —— lookahead 分裂 → 合并回 LR(0)，0/0 冲突
bool testClassicLR()
{
    auto g = makeHandGrammar(
        { {"S", SymbolKind::Nonterminal}, {"L", SymbolKind::Nonterminal},
          {"R", SymbolKind::Nonterminal}, {"=", SymbolKind::Terminal},
          {"*", SymbolKind::Terminal}, {"id", SymbolKind::Terminal} },
        { {2, 3, 5, 4}, {2, 4}, {3, 6, 4}, {3, 7}, {4, 3} }, 2);
    auto ff = FirstFollow().compute(g);
    CHECK(ff.ok);
    auto lr0 = Lr0Automaton().build(g);
    CHECK(lr0.ok);
    auto lr = Lalr1Builder().build(g, ff.value, lr0.value);
    CHECK(lr.ok);
    const auto& data = lr.value;

    CHECK(lr0.value.states.size() == 10);
    CHECK(data.intermediateLr1States.size() == 14);
    CHECK(data.states.size() == 10);
    CHECK(data.states.size() == lr0.value.states.size());

    const auto confLr1 = countConflicts(g, lr0.value, data.intermediateLr1States,
                                        data.intermediateLr1Transitions);
    const auto confMerged = countConflicts(g, lr0.value, flattenMerged(data.states),
                                           data.transitions);
    CHECK(confLr1.shiftReduce == 0 && confLr1.reduceReduce == 0);
    CHECK(confMerged.shiftReduce == confLr1.shiftReduce);
    CHECK(confMerged.reduceReduce == confLr1.reduceReduce);

    CHECK(checkInvariants(g, lr0.value, data));
    return true;
}

// E-4：教科书 LALR 反例 S->aAd|bBd|aBe|bAe, A->c, B->c —— 合并引入 R/R=2 且 S/R 不新增
bool testLalrNotLr1()
{
    auto g = makeHandGrammar(
        { {"S", SymbolKind::Nonterminal}, {"A", SymbolKind::Nonterminal},
          {"B", SymbolKind::Nonterminal}, {"a", SymbolKind::Terminal},
          {"b", SymbolKind::Terminal}, {"c", SymbolKind::Terminal},
          {"d", SymbolKind::Terminal}, {"e", SymbolKind::Terminal} },
        { {2, 5, 3, 8}, {2, 6, 4, 8}, {2, 5, 4, 9}, {2, 6, 3, 9},
          {3, 7}, {4, 7} }, 2);
    auto ff = FirstFollow().compute(g);
    CHECK(ff.ok);
    auto lr0 = Lr0Automaton().build(g);
    CHECK(lr0.ok);
    auto lr = Lalr1Builder().build(g, ff.value, lr0.value);
    CHECK(lr.ok);
    const auto& data = lr.value;

    CHECK(lr0.value.states.size() == 13);
    CHECK(data.intermediateLr1States.size() == 14);
    CHECK(data.states.size() == 13);

    // LR(1) 无冲突；合并引入 R/R=2（s6，A->c vs B->c，lookahead d 与 e），S/R 不新增
    const auto confLr1 = countConflicts(g, lr0.value, data.intermediateLr1States,
                                        data.intermediateLr1Transitions);
    const auto confMerged = countConflicts(g, lr0.value, flattenMerged(data.states),
                                           data.transitions);
    CHECK(confLr1.shiftReduce == 0 && confLr1.reduceReduce == 0);
    CHECK(confMerged.shiftReduce == confLr1.shiftReduce);   // 关键性质：S/R 绝不新增
    CHECK(confMerged.reduceReduce == 2);
    CHECK(confMerged.reduceReduce >= confLr1.reduceReduce);

    // 冲突定位：合并 s6 中 [A->c·] 与 [B->c·] 的 lookahead 均含 d 和 e
    const int aProd = findProd(g, idOf(g, "A", SymbolKind::Nonterminal),
                               { idOf(g, "c", SymbolKind::Terminal) });
    const int bProd = findProd(g, idOf(g, "B", SymbolKind::Nonterminal),
                               { idOf(g, "c", SymbolKind::Terminal) });
    const int d = idOf(g, "d", SymbolKind::Terminal);
    const int e = idOf(g, "e", SymbolKind::Terminal);
    CHECK(aProd >= 0 && bProd >= 0 && d >= 0 && e >= 0);
    const Lalr1Item* a6 = findItem(data.states[6], aProd, 1);
    const Lalr1Item* b6 = findItem(data.states[6], bProd, 1);
    CHECK(a6 != nullptr && b6 != nullptr);
    if (a6) {
        CHECK(a6->lookaheads.count(d) == 1 && a6->lookaheads.count(e) == 1);
    }
    if (b6) {
        CHECK(b6->lookaheads.count(d) == 1 && b6->lookaheads.count(e) == 1);
    }

    CHECK(checkInvariants(g, lr0.value, data));
    return true;
}

// E-5：边界 —— 平衡括号 S->(S)S|ε（6→10→6，0/0）
bool testBalancedParen()
{
    auto g = makeHandGrammar(
        { {"S", SymbolKind::Nonterminal}, {"(", SymbolKind::Terminal},
          {")", SymbolKind::Terminal} },
        { {2, 3, 2, 4, 2}, {2} }, 2);
    auto ff = FirstFollow().compute(g);
    CHECK(ff.ok);
    auto lr0 = Lr0Automaton().build(g);
    CHECK(lr0.ok);
    auto lr = Lalr1Builder().build(g, ff.value, lr0.value);
    CHECK(lr.ok);
    const auto& data = lr.value;

    CHECK(lr0.value.states.size() == 6);
    CHECK(data.intermediateLr1States.size() == 10);
    CHECK(data.states.size() == 6);

    const auto confMerged = countConflicts(g, lr0.value, flattenMerged(data.states),
                                           data.transitions);
    CHECK(confMerged.shiftReduce == 0 && confMerged.reduceReduce == 0);
    CHECK(checkInvariants(g, lr0.value, data));
    return true;
}

// E-6：边界 —— 单产生式 S->a（3→3→3）
bool testSingleProduction()
{
    auto g = makeHandGrammar(
        { {"S", SymbolKind::Nonterminal}, {"a", SymbolKind::Terminal} },
        { {2, 3} }, 2);
    auto ff = FirstFollow().compute(g);
    CHECK(ff.ok);
    auto lr0 = Lr0Automaton().build(g);
    CHECK(lr0.ok);
    auto lr = Lalr1Builder().build(g, ff.value, lr0.value);
    CHECK(lr.ok);
    CHECK(lr.value.intermediateLr1States.size() == 3);
    CHECK(lr.value.states.size() == 3);
    CHECK(checkInvariants(g, lr0.value, lr.value));
    return true;
}

// E-7：边界 —— 纯 ε S->#（2→2→2），初始态含完成项 [S->·]
bool testEpsilonOnly()
{
    auto g = makeHandGrammar(
        { {"S", SymbolKind::Nonterminal} },
        { {2} }, 2);
    auto ff = FirstFollow().compute(g);
    CHECK(ff.ok);
    auto lr0 = Lr0Automaton().build(g);
    CHECK(lr0.ok);
    auto lr = Lalr1Builder().build(g, ff.value, lr0.value);
    CHECK(lr.ok);
    CHECK(lr.value.intermediateLr1States.size() == 2);
    CHECK(lr.value.states.size() == 2);
    CHECK(lr.value.states[0].items.count(Lalr1Item{ 0, 0, std::set<int>{0} }) == 1);
    CHECK(checkInvariants(g, lr0.value, lr.value));
    return true;
}

// E-8：边界 —— 错误路径 stage="lalr1-build"
bool testInvalidGrammar()
{
    auto g = makeHandGrammar(
        { {"S", SymbolKind::Nonterminal}, {"a", SymbolKind::Terminal} },
        { {2, 3} }, 2);
    auto ff = FirstFollow().compute(g);
    CHECK(ff.ok);
    auto lr0 = Lr0Automaton().build(g);
    CHECK(lr0.ok);

    // startSymbol 无效
    auto gBad = g;
    gBad.startSymbol = -1;
    auto r1 = Lalr1Builder().build(gBad, ff.value, lr0.value);
    CHECK(!r1.ok);
    CHECK(hasStageError(r1, "lalr1-build"));

    // 产生式为空
    auto gEmpty = g;
    gEmpty.productions.clear();
    auto r2 = Lalr1Builder().build(gEmpty, ff.value, lr0.value);
    CHECK(!r2.ok);
    CHECK(hasStageError(r2, "lalr1-build"));

    // FirstFollowSets 尺寸与 Grammar 不符
    auto badSets = ff.value;
    badSets.first.clear();
    auto r3 = Lalr1Builder().build(g, badSets, lr0.value);
    CHECK(!r3.ok);
    CHECK(hasStageError(r3, "lalr1-build"));

    // Lr0AutomatonData 为空
    Lr0AutomatonData emptyLr0;
    auto r4 = Lalr1Builder().build(g, ff.value, emptyLr0);
    CHECK(!r4.ok);
    CHECK(hasStageError(r4, "lalr1-build"));

    // 增广信息与 Grammar 不一致（rhs 非 {start}）
    auto badAug = lr0.value;
    badAug.augmentedProductionRhs.clear();
    auto r5 = Lalr1Builder().build(g, ff.value, badAug);
    CHECK(!r5.ok);
    CHECK(hasStageError(r5, "lalr1-build"));
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
    ok &= testClassicLR();
    ok &= testLalrNotLr1();
    ok &= testBalancedParen();
    ok &= testSingleProduction();
    ok &= testEpsilonOnly();
    ok &= testInvalidGrammar();

    if (!ok) {
        std::cerr << "FAIL: 存在未通过的 CHECK（已通过 " << g_checks
                  << " 项检查）" << std::endl;
        return 1;
    }
    std::cout << "lalr1_test OK, checks=" << g_checks << std::endl;
    return 0;
}
