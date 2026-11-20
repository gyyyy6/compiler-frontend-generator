// T2.9 单测：IrGenerator（ParseTree → 四元式 IR）。
// 零 Qt、纯 STL；沿用 T1.x/T2.x 冒烟约定：g++ 独立编译运行，退出码 0 = 全部通过。
// 用法：ir_generator_test <languages 根目录>（缺省 ./test/languages）
//
// 覆盖（docs/PROGRESS.md T2.9 推导锚点 + DATA_STRUCTURES §5.7）：
//   TINY / Mini-C 全链路：rules(registry) → bnf → 化简 → First/Follow → LR(0)
//     → LALR(1) → 分析表 → Scanner(test.<lang>) → LrDriver → IrGenerator；
//     四元式序列逐条比对锚点（TINY 41 quads / Mini-C 139 quads）；
//   手工例 E->E+T|T; T->T*F|F; F->n（n+n*n）→ 2 quads；
//   前缀 ++/-- 返回自增后值、后缀 v++/v-- 返回自增前值（区分处理）；
//   数组下标左值：[]= 写回、右值 [] 读入临时变量、连续赋值右结合、++arr[i]（T3.2）；
//   防御路径：空语法树 / 未知节点符号 → stage="ir" 错误。

#include "BnfParser.h"
#include "FirstFollow.h"
#include "GrammarSimplifier.h"
#include "IrGenerator.h"
#include "Lalr1Builder.h"
#include "LexerBuilder.h"
#include "LexRulesParser.h"
#include "Lr0Automaton.h"
#include "LrDriver.h"
#include "ParseTable.h"
#include "Scanner.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using compiler::Symbol;
using compiler::SymbolKind;
using compiler::Token;
using compiler::TokenCodeRegistry;
using compiler::grammar::BnfParser;
using compiler::grammar::FirstFollow;
using compiler::grammar::GrammarSimplifier;
using compiler::ir::IrGenerator;
using compiler::ir::IrProgram;
using compiler::ir::Quadruple;
using compiler::lexer::LexerBuilder;
using compiler::lexer::LexRulesParser;
using compiler::lexer::Scanner;
using compiler::parser::Lalr1Builder;
using compiler::parser::Lr0Automaton;
using compiler::parser::LrDriver;
using compiler::parser::ParseTableBuilder;
using compiler::parser::ParseTableData;
using compiler::ParseTree;
using compiler::ParseTreeNode;

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

bool hasStageError(const compiler::Result<IrProgram>& r, const std::string& stage)
{
    for (const auto& d : r.diagnostics) {
        if (d.severity == compiler::Severity::Error && d.stage == stage) {
            return true;
        }
    }
    return false;
}

Token makeTok(int code, const std::string& name, const std::string& lexeme)
{
    Token t;
    t.code = code;
    t.name = name;
    t.lexeme = lexeme;
    return t;
}

// 手工文法：终结符 tokenCode 取 symbolId（token.code → symbolId 映射用）
void assignTerminalCodes(compiler::Grammar& g)
{
    for (Symbol& s : g.symbols) {
        if (s.kind == SymbolKind::Terminal) {
            s.tokenCode = s.id;
        }
    }
}

// 全链路（Grammar 已含终结符 tokenCode）→ 分析表
compiler::Result<ParseTableData> buildTableFor(const compiler::Grammar& g)
{
    auto ff = FirstFollow().compute(g);
    if (!ff.ok) {
        return {};
    }
    auto lr0 = Lr0Automaton().build(g);
    if (!lr0.ok) {
        return {};
    }
    auto lr = Lalr1Builder().build(g, ff.value, lr0.value);
    if (!lr.ok) {
        return {};
    }
    return ParseTableBuilder().build(g, lr.value);
}

// 语言全链路产物（grammar + 表 + 扫描 token 流（含 EOF））
struct FullChain {
    bool ok = false;
    compiler::Grammar g;
    ParseTableData table;
    std::vector<Token> tokens;   // Scanner 产出，含末尾 EOF
};

FullChain buildChain(const std::string& langRoot, const std::string& lang,
                     const std::string& sourceText = std::string())
{
    FullChain ch;
    LexRulesParser rp;
    auto rr = rp.parseFile(langRoot + "/" + lang + "/" + lang + ".rules");
    if (!rr.ok) {
        return ch;
    }
    TokenCodeRegistry registry = rr.value.registry;

    auto bnf = BnfParser().parseFile(langRoot + "/" + lang + "/" + lang + ".bnf",
                                     &registry);
    if (!bnf.ok) {
        return ch;
    }
    auto sg = GrammarSimplifier().simplify(bnf.value);
    if (!sg.ok) {
        return ch;
    }
    ch.g = sg.value.grammar;

    auto table = buildTableFor(ch.g);
    if (!table.ok) {
        return ch;
    }
    ch.table = table.value;

    const std::string srcPath = sourceText.empty()
        ? langRoot + "/" + lang + "/samples/test." + lang
        : "<inline:" + lang + ">";
    const std::string source = sourceText.empty() ? loadFile(srcPath) : sourceText;
    LexerBuilder builder;
    auto dfar = builder.buildMinimalDfa(rr.value);
    if (!dfar.ok) {
        return ch;
    }
    Scanner scanner;
    if (!scanner.configure(rr.value, dfar.value)) {
        return ch;
    }
    auto sr = scanner.scan(source, srcPath);
    if (!sr.ok) {
        return ch;
    }
    ch.tokens = std::move(sr.tokens);
    ch.ok = true;
    return ch;
}

// 四元式 → "(op,arg1,arg2,result)" 渲染
std::string quadStr(const Quadruple& q)
{
    return "(" + q.op + "," + q.arg1 + "," + q.arg2 + "," + q.result + ")";
}

std::vector<std::string> render(const IrProgram& p)
{
    std::vector<std::string> out;
    out.reserve(p.quads.size());
    for (const Quadruple& q : p.quads) {
        out.push_back(quadStr(q));
    }
    return out;
}

// 空格分隔的锚点序列 → 向量（与 PROGRESS.md 记号一致）
std::vector<std::string> splitQuads(const std::string& s)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ' ') {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) {
        out.push_back(cur);
    }
    return out;
}

bool checkQuads(const IrProgram& p, const std::vector<std::string>& expected)
{
    const std::vector<std::string> got = render(p);
    if (got.size() != expected.size()) {
        std::cerr << "  quad 数量不符: got=" << got.size()
                  << " expected=" << expected.size() << std::endl;
        return false;
    }
    for (size_t i = 0; i < got.size(); ++i) {
        if (got[i] != expected[i]) {
            std::cerr << "  第 " << (i + 1) << " 条不符: got=" << got[i]
                      << " expected=" << expected[i] << std::endl;
            return false;
        }
    }
    return true;
}

// 手工语法树节点构造（生成器按符号名分发，productionId 不参与判断）
std::unique_ptr<ParseTreeNode> mkNode(int symbolId)
{
    auto n = std::make_unique<ParseTreeNode>();
    n->symbolId = symbolId;
    n->productionId = 0;
    n->tokenIndex = -1;
    return n;
}

std::unique_ptr<ParseTreeNode> mkLeaf(int symbolId, const std::string& text)
{
    auto n = std::make_unique<ParseTreeNode>();
    n->symbolId = symbolId;
    n->productionId = -1;
    n->tokenIndex = -1;
    n->text = text;
    return n;
}

// A-1：TINY 全链路 → 41 quads（PROGRESS.md T2.9 锚点完整序列）
bool testTiny(const std::string& langRoot)
{
    FullChain ch = buildChain(langRoot, "tiny");
    CHECK(ch.ok);

    auto pr = LrDriver().parse(ch.g, ch.table, ch.tokens);
    CHECK(pr.ok);
    auto ir = IrGenerator().generate(ch.g, pr.value);
    CHECK(ir.ok);

    const std::vector<std::string> expected = splitQuads(
        "(<=,x,10,t1) (jfalse,t1,,L1) (=,1,,y) (*,y,2,t2) (+,x,t2,t3) "
        "(/,10,5,t4) (%,t4,3,t5) (-,t3,t5,t6) (=,t6,,z) (^,x,2,t7) "
        "(=,t7,,p) (read,,,w) (+,w,z,t8) (write,t8,,) (goto,,,L2) "
        "(label,,,L1) (=,2,,y) (!=,y,z,t9) (write,t9,,) (label,,,L2) "
        "(label,,,L3) (-,x,1,t10) (=,t10,,x) (write,x,,) (<=,x,0,t11) "
        "(jfalse,t11,,L3) (read,,,a) (>=,a,5,t12) (=,t12,,b) (<,a,b,t13) "
        "(=,t13,,c) (>,a,b,t14) (=,t14,,d) (==,a,b,t15) (=,t15,,e) "
        "(+,a,b,t16) (*,t16,2,t17) (write,t17,,) (jfalse,f,,L4) (=,1,,g) "
        "(label,,,L4)");
    CHECK(expected.size() == 41);
    CHECK(checkQuads(ir.value, expected));

    std::cout << "info[tiny] quads=" << ir.value.quads.size() << std::endl;
    return true;
}

// A-2：Mini-C 全链路 → 139 quads（t1~t46、L1~L19 + add/zero/total/empty/main 函数 label）
bool testMinic(const std::string& langRoot)
{
    FullChain ch = buildChain(langRoot, "minic");
    CHECK(ch.ok);

    auto pr = LrDriver().parse(ch.g, ch.table, ch.tokens);
    CHECK(pr.ok);
    auto ir = IrGenerator().generate(ch.g, pr.value);
    CHECK(ir.ok);

    const std::vector<std::string> expected = splitQuads(
        "(label,add,,) (+,x,y,t1) (ret,t1,,) (label,zero,,) "
        "(ret,0,,) (label,total,,) ([],values,0,t2) (+,t2,n,t3) "
        "(ret,t3,,) (label,empty,,) (ret,,,) (label,main,,) "
        "(=,3.14,,pi) (=,0,,sum) (=,0,,i) (=,1,,_tmp) "
        "(=,_tmp,,a_1) (=,a_1,,iffy) (=,iffy,,format) (label,,,L1) "
        "(<,i,10,t4) (jfalse,t4,,L2) (*,i,2,t5) (+,sum,t5,t6) "
        "(/,10,5,t7) (%,t7,3,t8) (-,t6,t8,t9) (=,t9,,sum) "
        "(+,i,1,t10) (=,t10,,i) (goto,,,L1) (label,,,L2) "
        "(=,0,,i) (label,,,L3) (<,i,10,t11) (jfalse,t11,,L4) "
        "(param,sum,,) (param,i,,) (call,add,,t12) (+,sum,t12,t13) "
        "(=,t13,,sum) (param,arr,,) (param,i,,) (call,total,,t14) "
        "(=,t14,,k) (+,i,1,t15) (=,t15,,i) (goto,,,L3) "
        "(label,,,L4) (label,,,L5) (<,i,12,t16) (jfalse,t16,,L6) "
        "(+,sum,1,t17) (=,t17,,sum) (+,i,1,t18) (=,t18,,i) "
        "(goto,,,L5) (label,,,L6) (=,0,,i) (label,,,L7) "
        "(<,i,2,t19) (jfalse,t19,,L8) (+,i,1,t20) (=,t20,,i) "
        "(goto,,,L7) (label,,,L8) (=,0,,i) (label,,,L9) "
        "(<,i,2,t21) (jfalse,t21,,L10) (+,sum,k,t22) (=,t22,,sum) "
        "(+,i,1,t23) (=,t23,,i) (+,k,1,t24) (=,t24,,k) "
        "(goto,,,L9) (label,,,L10) (label,,,L11) (<,q,1,t25) "
        "(jfalse,t25,,L12) (+,sum,q,t26) (=,t26,,sum) (+,q,1,t27) "
        "(=,t27,,q) (goto,,,L11) (label,,,L12) (<,sum,0,t28) "
        "(jfalse,t28,,L13) (=,0,,sum) (label,,,L13) (==,sum,100,t29) "
        "(jfalse,t29,,L14) (+,sum,1,t30) (=,t30,,sum) (goto,,,L15) "
        "(label,,,L14) (-,sum,1,t31) (=,t31,,sum) (label,,,L15) "
        "(>,sum,0,t32) (jfalse,t32,,L16) (>,sum,10,t33) (jfalse,t33,,L18) "
        "(-,sum,1,t34) (=,t34,,sum) (goto,,,L19) (label,,,L18) "
        "(+,sum,1,t35) (=,t35,,sum) (label,,,L19) (goto,,,L17) "
        "(label,,,L16) (=,0,,sum) (label,,,L17) ([]=,1,0,arr) "
        "([],arr,2,t36) (>=,t36,5,t37) (=,t37,,a) (^,pi,2,t38) "
        "(=,t38,,d) (+,a,i,t39) (*,t39,1.5,t40) (=,t40,,f) "
        "(+,i,1,t41) (=,t41,,i) (=,t41,,k) (-,i,1,t42) "
        "(=,t42,,i) (=,t42,,k) (!=,i,0,t43) (=,t43,,k) "
        "(<=,i,10,t44) (=,t44,,k) (>,i,0,t45) (=,t45,,k) "
        "(call,zero,,t46) (=,t46,,k) (ret,,,)");
    CHECK(expected.size() == 139);
    CHECK(checkQuads(ir.value, expected));

    std::cout << "info[minic] quads=" << ir.value.quads.size() << std::endl;
    return true;
}
// A-3：手工例 E->E+T|T; T->T*F|F; F->n（n+n*n）→ 2 quads（F 因子化，与语言包文法同构）
bool testHandExpr()
{
    auto g = makeHandGrammar(
        { {"E", SymbolKind::Nonterminal},
          {"T", SymbolKind::Nonterminal},
          {"F", SymbolKind::Nonterminal},
          {"SYM_PLUS", SymbolKind::Terminal},
          {"SYM_TIMES", SymbolKind::Terminal},
          {"n", SymbolKind::Terminal} },
        {}, 2);
    // 手工产生式：E -> E + T | T；T -> T * F | F；F -> n
    auto add = [&g](int lhs, const std::vector<int>& rhs) {
        compiler::Production p;
        p.id = static_cast<int>(g.productions.size());
        p.lhs = lhs;
        p.rhs = rhs;
        g.productions.push_back(std::move(p));
    };
    add(2, {2, 5, 3});    // E -> E + T
    add(2, {3});          // E -> T
    add(3, {3, 6, 4});    // T -> T * F
    add(3, {4});          // T -> F
    add(4, {7});          // F -> n
    assignTerminalCodes(g);

    // n + n * n
    std::vector<Token> in = {
        makeTok(7, "n", "n"), makeTok(5, "SYM_PLUS", "+"),
        makeTok(7, "n", "n"), makeTok(6, "SYM_TIMES", "*"),
        makeTok(7, "n", "n"), makeTok(0, "EOF", "$"),
    };
    auto tr = buildTableFor(g);
    CHECK(tr.ok);
    auto pr = LrDriver().parse(g, tr.value, in);
    CHECK(pr.ok);
    auto ir = IrGenerator().generate(g, pr.value);
    CHECK(ir.ok);
    CHECK(checkQuads(ir.value, splitQuads("(*,n,n,t1) (+,n,t1,t2)")));

    std::cout << "info[hand] quads=" << ir.value.quads.size() << std::endl;
    return true;
}

// A-4：前后缀 ++/-- 语义区分（Mini-C 文法上手工构造赋值树）：
//   前缀 ++v 返回自增后值；后缀 v++ 返回自增前值（先自增、返回旧值）。
bool testIncDecSemantics(const std::string& langRoot)
{
    auto bnf = BnfParser().parseFile(langRoot + "/minic/minic.bnf");
    CHECK(bnf.ok);
    const compiler::Grammar& g = bnf.value;
    const int eId = g.symbolId("expression", SymbolKind::Nonterminal);
    const int fId = g.symbolId("factor", SymbolKind::Nonterminal);
    const int vId = g.symbolId("variable", SymbolKind::Nonterminal);
    const int incId = g.symbolId("SYM_INC", SymbolKind::Terminal);
    const int asId = g.symbolId("SYM_ASSIGN", SymbolKind::Terminal);
    const int idId = g.symbolId("ID", SymbolKind::Terminal);
    CHECK(eId >= 0 && fId >= 0 && vId >= 0 && incId >= 0 && asId >= 0 && idId >= 0);

    auto buildTree = [&](bool prefix) {
        ParseTree tree;
        auto expr = mkNode(eId);                   // expression（赋值）
        auto lhs = mkNode(vId);                    // variable = k
        lhs->children.push_back(mkLeaf(idId, "k"));
        auto rhs = mkNode(fId);                    // factor（++v / v++）
        auto var = mkNode(vId);                    // variable = v
        var->children.push_back(mkLeaf(idId, "v"));
        if (prefix) {
            rhs->children.push_back(mkLeaf(incId, "++"));
            rhs->children.push_back(std::move(var));
        } else {
            rhs->children.push_back(std::move(var));
            rhs->children.push_back(mkLeaf(incId, "++"));
        }
        expr->children.push_back(std::move(lhs));
        expr->children.push_back(mkLeaf(asId, "="));
        expr->children.push_back(std::move(rhs));
        tree.root = std::move(expr);
        return tree;
    };

    // 前缀 k = ++v：返回自增后值 t1
    auto r1 = IrGenerator().generate(g, buildTree(true));
    CHECK(r1.ok);
    CHECK(checkQuads(r1.value, splitQuads("(+,v,1,t1) (=,t1,,v) (=,t1,,k)")));

    // 后缀 k = v++：返回自增前值 v
    auto r2 = IrGenerator().generate(g, buildTree(false));
    CHECK(r2.ok);
    CHECK(checkQuads(r2.value, splitQuads("(+,v,1,t1) (=,t1,,v) (=,v,,k)")));
    return true;
}

// A-5：数组下标左值（T3.2）：
//   右值 arr[i] → ([] , arr, i, t)；左值 arr[i] = v → ([]=, v, i, arr)；
//   混合表达式 x = arr[i] + 1 必须先读入临时变量；连续赋值右结合。
bool testArrayLvalueSemantics(const std::string& langRoot)
{
    auto bnf = BnfParser().parseFile(langRoot + "/minic/minic.bnf");
    CHECK(bnf.ok);
    const compiler::Grammar& g = bnf.value;

    const int eId = g.symbolId("expression", SymbolKind::Nonterminal);
    const int seId = g.symbolId("simple-expression", SymbolKind::Nonterminal);
    const int addId = g.symbolId("additive-expression", SymbolKind::Nonterminal);
    const int termId = g.symbolId("term", SymbolKind::Nonterminal);
    const int powerId = g.symbolId("power", SymbolKind::Nonterminal);
    const int fId = g.symbolId("factor", SymbolKind::Nonterminal);
    const int vId = g.symbolId("variable", SymbolKind::Nonterminal);
    const int addopId = g.symbolId("addop", SymbolKind::Nonterminal);
    const int idId = g.symbolId("ID", SymbolKind::Terminal);
    const int numId = g.symbolId("NUM", SymbolKind::Terminal);
    const int asId = g.symbolId("SYM_ASSIGN", SymbolKind::Terminal);
    const int lbId = g.symbolId("SYM_LBRACKET", SymbolKind::Terminal);
    const int rbId = g.symbolId("SYM_RBRACKET", SymbolKind::Terminal);
    const int plusId = g.symbolId("SYM_PLUS", SymbolKind::Terminal);
    CHECK(eId >= 0 && seId >= 0 && addId >= 0 && termId >= 0 && powerId >= 0 &&
          fId >= 0 && vId >= 0 && addopId >= 0);
    CHECK(idId >= 0 && numId >= 0 && asId >= 0 && lbId >= 0 && rbId >= 0 &&
          plusId >= 0);

    // ---- 手工构造与 minic.bnf 同构的表达式树 ----

    auto mkVar = [&](const std::string& name) {
        auto v = mkNode(vId);
        v->children.push_back(mkLeaf(idId, name));
        return v;
    };
    auto mkIndexedVar = [&](const std::string& name,
                            std::unique_ptr<ParseTreeNode> indexExpr) {
        auto v = mkNode(vId);
        v->children.push_back(mkLeaf(idId, name));
        v->children.push_back(mkLeaf(lbId, "["));
        v->children.push_back(std::move(indexExpr));
        v->children.push_back(mkLeaf(rbId, "]"));
        return v;
    };
    auto mkVarFactor = [&](const std::string& name) {
        auto factor = mkNode(fId);
        factor->children.push_back(mkVar(name));
        return factor;
    };
    auto mkIndexedFactor = [&](const std::string& name,
                               std::unique_ptr<ParseTreeNode> indexExpr) {
        auto factor = mkNode(fId);
        factor->children.push_back(mkIndexedVar(name, std::move(indexExpr)));
        return factor;
    };
    auto mkNumFactor = [&](const std::string& text) {
        auto factor = mkNode(fId);
        factor->children.push_back(mkLeaf(numId, text));
        return factor;
    };
    auto mkTermFromFactor = [&](std::unique_ptr<ParseTreeNode> factor) {
        auto power = mkNode(powerId);
        power->children.push_back(std::move(factor));
        auto term = mkNode(termId);
        term->children.push_back(std::move(power));
        return term;
    };
    auto mkAdditiveFromTerm = [&](std::unique_ptr<ParseTreeNode> term) {
        auto add = mkNode(addId);
        add->children.push_back(std::move(term));
        return add;
    };
    auto mkExprFromAdd = [&](std::unique_ptr<ParseTreeNode> add) {
        auto simple = mkNode(seId);
        simple->children.push_back(std::move(add));
        auto expr = mkNode(eId);
        expr->children.push_back(std::move(simple));
        return expr;
    };
    auto mkExprFromFactor = [&](std::unique_ptr<ParseTreeNode> factor) {
        return mkExprFromAdd(
            mkAdditiveFromTerm(mkTermFromFactor(std::move(factor))));
    };
    auto mkNumExpr = [&](const std::string& text) {
        return mkExprFromFactor(mkNumFactor(text));
    };
    auto mkVarExpr = [&](const std::string& name) {
        return mkExprFromFactor(mkVarFactor(name));
    };
    auto mkBinaryAdd = [&](std::unique_ptr<ParseTreeNode> lhsAdd,
                           std::unique_ptr<ParseTreeNode> rhsTerm) {
        auto add = mkNode(addId);
        add->children.push_back(std::move(lhsAdd));
        auto op = mkNode(addopId);
        op->children.push_back(mkLeaf(plusId, "+"));
        add->children.push_back(std::move(op));
        add->children.push_back(std::move(rhsTerm));
        return add;
    };
    auto mkAssign = [&](std::unique_ptr<ParseTreeNode> lhs,
                        std::unique_ptr<ParseTreeNode> rhs) {
        auto expr = mkNode(eId);
        expr->children.push_back(std::move(lhs));
        expr->children.push_back(mkLeaf(asId, "="));
        expr->children.push_back(std::move(rhs));
        return expr;
    };
    auto runTree = [&](std::unique_ptr<ParseTreeNode> root) {
        ParseTree tree;
        tree.root = std::move(root);
        return IrGenerator().generate(g, tree);
    };

    // arr[0] = 1 → 左值写入，不产生多余的 [] 读
    auto r1 = runTree(mkAssign(mkIndexedVar("arr", mkNumExpr("0")),
                               mkNumExpr("1")));
    CHECK(r1.ok);
    CHECK(checkQuads(r1.value, splitQuads("([]=,1,0,arr)")));

    // a[i] = b + c → 先算右值，再按下标写回
    auto r2 = runTree(mkAssign(
        mkIndexedVar("a", mkVarExpr("i")),
        mkExprFromAdd(mkBinaryAdd(
            mkAdditiveFromTerm(mkTermFromFactor(mkVarFactor("b"))),
            mkTermFromFactor(mkVarFactor("c"))))));
    CHECK(r2.ok);
    CHECK(checkQuads(r2.value, splitQuads("(+,b,c,t1) ([]=,t1,i,a)")));

    // x = arr[i] + 1 → 右值读取生成临时变量 t1，再参与加法
    auto r3 = runTree(mkAssign(
        mkVar("x"),
        mkExprFromAdd(mkBinaryAdd(
            mkAdditiveFromTerm(mkTermFromFactor(
                mkIndexedFactor("arr", mkVarExpr("i")))),
            mkTermFromFactor(mkNumFactor("1"))))));
    CHECK(r3.ok);
    CHECK(checkQuads(r3.value,
                     splitQuads("([],arr,i,t1) (+,t1,1,t2) (=,t2,,x)")));

    // arr[0] = arr[1] = 1 → 右结合：先写 arr[1]，再把同一值写 arr[0]
    auto inner = mkAssign(mkIndexedVar("arr", mkNumExpr("1")), mkNumExpr("1"));
    auto r4 = runTree(mkAssign(mkIndexedVar("arr", mkNumExpr("0")),
                               std::move(inner)));
    CHECK(r4.ok);
    CHECK(checkQuads(r4.value, splitQuads("([]=,1,1,arr) ([]=,1,0,arr)")));

    return true;
}

// A-6：数组下标左值端到端（真实扫描器 + LALR 语法树）：
//   arr[0]=1; a[i]=b+c; x=arr[i]+1; arr[0]=arr[1]=1; k=++arr[i];
bool testArrayLvalueEndToEnd(const std::string& langRoot)
{
    const std::string src =
        "int arr[10];\n"
        "int a[10];\n"
        "int i;\n"
        "int x;\n"
        "int b;\n"
        "int c;\n"
        "int k;\n"
        "\n"
        "void main(void)\n"
        "{\n"
        "    arr[0] = 1;\n"
        "    a[i] = b + c;\n"
        "    x = arr[i] + 1;\n"
        "    arr[0] = arr[1] = 1;\n"
        "    k = ++arr[i];\n"
        "}\n";
    FullChain ch = buildChain(langRoot, "minic", src);
    CHECK(ch.ok);

    auto pr = LrDriver().parse(ch.g, ch.table, ch.tokens);
    CHECK(pr.ok);
    auto ir = IrGenerator().generate(ch.g, pr.value);
    CHECK(ir.ok);
    CHECK(checkQuads(ir.value, splitQuads(
        "(label,main,,) ([]=,1,0,arr) (+,b,c,t1) ([]=,t1,i,a) "
        "([],arr,i,t2) (+,t2,1,t3) (=,t3,,x) "
        "([]=,1,1,arr) ([]=,1,0,arr) "
        "([],arr,i,t4) (+,t4,1,t5) ([]=,t5,i,arr) (=,t5,,k) (ret,,,)")));
    return true;
}

// A-7：防御 —— 空语法树 → stage="ir"
bool testErrorEmptyTree()
{
    auto g = makeHandGrammar(
        { {"S", SymbolKind::Nonterminal}, {"a", SymbolKind::Terminal} },
        { {2, 3} }, 2);
    ParseTree tree;
    auto r = IrGenerator().generate(g, tree);
    CHECK(!r.ok);
    CHECK(hasStageError(r, "ir"));
    return true;
}

// A-8：防御 —— 未知节点符号（手工树 children 缺失）→ stage="ir"
bool testErrorUnknownNode()
{
    auto g = makeHandGrammar(
        { {"S", SymbolKind::Nonterminal}, {"a", SymbolKind::Terminal} },
        { {2, 3} }, 2);
    ParseTree tree;
    tree.root = std::make_unique<ParseTreeNode>();
    tree.root->symbolId = 2;       // S（非 program，且无子节点）
    tree.root->productionId = 0;
    auto r = IrGenerator().generate(g, tree);
    CHECK(!r.ok);
    CHECK(hasStageError(r, "ir"));
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
    const std::string tinySrc = loadFile(langRoot + "/tiny/samples/test.tiny");
    const std::string minicSrc = loadFile(langRoot + "/minic/samples/test.minic");
    if (tinyBnf.empty() || minicBnf.empty() || tinySrc.empty() || minicSrc.empty()) {
        std::cerr << "FAIL: 找不到语言包（" << langRoot
                  << "/tiny|minic/*.{bnf,rules,samples}）" << std::endl;
        return 1;
    }

    bool ok = true;
    ok &= testTiny(langRoot);
    ok &= testMinic(langRoot);
    ok &= testHandExpr();
    ok &= testIncDecSemantics(langRoot);
    ok &= testArrayLvalueSemantics(langRoot);
    ok &= testArrayLvalueEndToEnd(langRoot);
    ok &= testErrorEmptyTree();
    ok &= testErrorUnknownNode();

    if (!ok) {
        std::cerr << "FAIL: 存在未通过的 CHECK（已通过 " << g_checks
                  << " 项检查）" << std::endl;
        return 1;
    }
    std::cout << "ir_generator_test OK, checks=" << g_checks << std::endl;
    return 0;
}
