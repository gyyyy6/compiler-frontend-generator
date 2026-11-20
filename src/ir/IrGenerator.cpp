// 中间代码生成器实现（T2.9，依据 docs/PROGRESS.md T2.9 推导锚点 与 docs/DATA_STRUCTURES.md §5.7）
// 纯 C++17 + STL，不依赖 Qt。
// 递归下降式遍历 ParseTree：按节点符号名分发（兼容 TINY 与 Mini-C 两套命名），
// 表达式链统一为「操作数 op 操作数」的通用二元处理；控制流标签按名回填
// （jfalse/goto 先写目标标签，label 后发出），顺序与推导锚点逐条一致。
// 数组左值（T3.2）：赋值 / ++ / -- 左侧走 genLValue；写 []=，右值读 []。

#include "IrGenerator.h"

#include <string>
#include <utility>
#include <vector>

#include "Diagnostic.h"

namespace compiler::ir {

namespace {

// op 规范映射：终结符符号名 → 规范四元式操作符（PROGRESS.md T2.9 设计点 2）。
// SYM_EQ→==、SYM_NE→!=：TINY 的 `=`/`<>` 词素归一，避免与赋值 `=` 冲突。
const char* canonicalOp(const std::string& symName)
{
    if (symName == "SYM_PLUS")  return "+";
    if (symName == "SYM_MINUS") return "-";
    if (symName == "SYM_TIMES") return "*";
    if (symName == "SYM_DIV")   return "/";
    if (symName == "SYM_MOD")   return "%";
    if (symName == "SYM_POWER") return "^";
    if (symName == "SYM_LT")    return "<";
    if (symName == "SYM_GT")    return ">";
    if (symName == "SYM_LE")    return "<=";
    if (symName == "SYM_GE")    return ">=";
    if (symName == "SYM_EQ")    return "==";
    if (symName == "SYM_NE")    return "!=";
    return nullptr;
}

// 生成上下文：临时变量/标签单调分配 + 四元式缓冲。失败即停，只报第一个错误
// （与 T2.8 决策点 5 B 风格一致），错误 stage = "ir"。
class Gen {
public:
    explicit Gen(const Grammar& g) : g_(g) {}

    Result<IrProgram> run(const ParseTree& tree)
    {
        if (!tree.root) {
            fail("语法树为空");
        } else {
            genNode(tree.root.get());
        }
        if (failed_) {
            return Result<IrProgram>::failure(
                {Severity::Error, "ir", errorMsg_, {-1, -1, -1}});
        }
        IrProgram p;
        p.quads = std::move(quads_);
        return Result<IrProgram>::success(std::move(p));
    }

private:
    const Grammar& g_;
    std::vector<Quadruple> quads_;
    int tempNo_ = 0;
    int labelNo_ = 0;
    bool failed_ = false;
    std::string errorMsg_;

    void fail(const std::string& msg)
    {
        if (!failed_) {
            failed_ = true;
            errorMsg_ = msg;
        }
    }

    // 节点符号名（内部节点 = 非终结符名，叶子 = 终结符名）；空/越界返回空串
    std::string symName(const ParseTreeNode* n) const
    {
        if (!n) {
            return {};
        }
        const Symbol* s = g_.symbol(n->symbolId);
        return s ? s->name : std::string();
    }

    bool isName(const ParseTreeNode* n, const std::string& name) const
    {
        return symName(n) == name;
    }

    std::string newTemp()  { return "t" + std::to_string(++tempNo_); }
    std::string newLabel() { return "L" + std::to_string(++labelNo_); }

    void emit(const std::string& op, const std::string& a1,
              const std::string& a2, const std::string& res)
    {
        if (failed_) {
            return;
        }
        quads_.push_back({op, a1, a2, res});
    }

    // 操作符节点 opText 归一（设计点 1/2）：操作符挂在非终结符节点上
    // （addop/relop/comparison-op/mulop/powop），取其中唯一终结符叶子，
    // 按终结符符号名归一；未知操作符兜底用词素原样（锚点语言包不会触发）。
    std::string opText(const ParseTreeNode* n)
    {
        if (failed_) {
            return {};
        }
        const ParseTreeNode* leaf = n;
        if (!leaf->isLeaf() && leaf->children.size() == 1) {
            leaf = leaf->children[0].get();
        }
        if (!leaf->isLeaf()) {
            fail("操作符节点形状异常（应恰有 1 个终结符叶子）");
            return {};
        }
        const Symbol* s = g_.symbol(leaf->symbolId);
        if (s) {
            if (const char* op = canonicalOp(s->name)) {
                return op;
            }
        }
        return leaf->text;
    }

    // ---- 顶层分发：按节点符号名路由 ----
    std::string genNode(const ParseTreeNode* n)
    {
        if (failed_ || !n) {
            return {};
        }
        const std::string name = symName(n);

        // 程序/结构类（不返回值）
        if (name == "program") {
            if (n->children.size() != 1) {
                fail("program 节点应恰有 1 个子节点");
                return {};
            }
            genNode(n->children[0].get());
            return {};
        }
        if (name == "stmt-sequence" || name == "statement-list") {
            genStmtSeq(n);
            return {};
        }
        if (name == "definition-list" || name == "local-definitions") {
            genDefList(n);
            return {};
        }
        if (name == "definition") {
            if (n->children.size() != 1) {
                fail("definition 节点应恰有 1 个子节点");
                return {};
            }
            genNode(n->children[0].get());
            return {};
        }
        if (name == "variable-definition") {
            return {};   // 变量定义不产生代码
        }
        if (name == "function-definition") {
            genFunction(n);
            return {};
        }
        if (name == "compound-stmt") {
            genCompound(n);
            return {};
        }
        if (name == "statement") {
            if (n->children.size() != 1) {
                fail("statement 节点应恰有 1 个子节点");
                return {};
            }
            genNode(n->children[0].get());
            return {};
        }
        if (name == "if-stmt" || name == "condition-stmt") {
            genIf(n);
            return {};
        }
        if (name == "repeat-stmt") {
            genRepeat(n);
            return {};
        }
        if (name == "assign-stmt") {
            genAssign(n);
            return {};
        }
        if (name == "read-stmt") {
            genRead(n);
            return {};
        }
        if (name == "write-stmt") {
            genWrite(n);
            return {};
        }
        if (name == "while-stmt") {
            genWhile(n);
            return {};
        }
        if (name == "for-stmt") {
            genFor(n);
            return {};
        }
        if (name == "return-stmt") {
            genReturn(n);
            return {};
        }
        if (name == "expression-stmt") {
            genExprStmt(n);
            return {};
        }
        if (name == "for-init" || name == "for-update") {
            if (!n->children.empty()) {
                genNode(n->children[0].get());
            }
            return {};
        }
        if (name == "expression-list") {
            genExpressionList(n);
            return {};
        }
        if (name == "arguments" || name == "argument-list") {
            genArguments(n);
            return {};
        }
        if (name == "parameters" || name == "parameter-list" ||
            name == "parameter" || name == "type-indicator") {
            return {};   // 形参/类型不产生代码
        }

        // 表达式类（返回地址串：变量名 / 数值词素 / 临时变量）
        if (name == "exp" || name == "simple-exp" || name == "simple-expression" ||
            name == "additive-expression" || name == "term" || name == "power") {
            return genChain(n);
        }
        if (name == "expression") {
            return genExpression(n);
        }
        if (name == "variable") {
            return genVariable(n);
        }
        if (name == "call") {
            return genCall(n);
        }
        if (name == "factor") {
            return genFactor(n);
        }
        if (name == "addop" || name == "relop" || name == "comparison-op" ||
            name == "mulop" || name == "powop") {
            return opText(n);   // 防御：操作符节点被顶层触达
        }
        // 兜底（手工推导文法 E->E+T|T; T->T*F|F; F->n 等以表达式非终结符为根）：
        //   真叶子（productionId == -1）→ 返回词素；
        //   1/3 子节点的未知内部节点 → 按表达式链处理；
        //   其余形状一律报错（语言包全部节点名已显式处理，不触达本分支）。
        if (n->productionId == -1 && n->isLeaf()) {
            return n->text;
        }
        if (n->children.size() == 1 || n->children.size() == 3) {
            return genChain(n);
        }
        fail("未知节点符号: " + name);
        return {};
    }

    // ---- 语句 ----

    // 语句序列（TINY stmt-sequence / Mini-C statement-list）：左递归链按源顺序
    // 生成；SYM_SEMI 为分隔符跳过。
    void genStmtSeq(const ParseTreeNode* n)
    {
        for (const auto& c : n->children) {
            if (failed_) {
                return;
            }
            const std::string nm = symName(c.get());
            if (nm == "SYM_SEMI") {
                continue;
            }
            if (nm == "stmt-sequence" || nm == "statement-list") {
                genStmtSeq(c.get());
                continue;
            }
            genNode(c.get());
        }
    }

    // 定义列表 / 局部定义：左递归链，只下钻到产生代码的节点（变量定义无代码）
    void genDefList(const ParseTreeNode* n)
    {
        for (const auto& c : n->children) {
            if (failed_) {
                return;
            }
            genNode(c.get());
        }
    }

    // if（TINY if-stmt / Mini-C condition-stmt）：
    //   KW_IF <cond> KW_THEN <then> [KW_ELSE <else>] KW_END（TINY）
    //   KW_IF ( <cond> ) <then> [KW_ELSE <else>]（Mini-C）
    // 排除关键字/括号后按出现顺序取条件与语句体。
    void genIf(const ParseTreeNode* n)
    {
        const ParseTreeNode* cond = nullptr;
        std::vector<const ParseTreeNode*> stmts;
        for (const auto& c : n->children) {
            if (failed_) {
                return;
            }
            const std::string nm = symName(c.get());
            if (nm == "KW_IF" || nm == "KW_THEN" || nm == "KW_ELSE" ||
                nm == "KW_END" || nm == "SYM_LPAREN" || nm == "SYM_RPAREN") {
                continue;
            }
            if (!cond) {
                cond = c.get();
            } else {
                stmts.push_back(c.get());
            }
        }
        if (!cond || stmts.empty() || stmts.size() > 2) {
            fail("if 结构异常（缺条件或语句体）");
            return;
        }
        std::string c = genNode(cond);
        if (stmts.size() == 1) {
            // 无 else：不分配结束标签，jfalse 目标标签兼作结束（设计点 3）
            std::string lEnd = newLabel();
            emit("jfalse", c, "", lEnd);
            genNode(stmts[0]);
            emit("label", "", "", lEnd);
        } else {
            std::string lElse = newLabel();
            std::string lEnd = newLabel();
            emit("jfalse", c, "", lElse);
            genNode(stmts[0]);
            emit("goto", "", "", lEnd);
            emit("label", "", "", lElse);
            genNode(stmts[1]);
            emit("label", "", "", lEnd);
        }
    }

    // repeat-until（TINY）：label body；body；cond→t；jfalse t → body（设计点 5）
    void genRepeat(const ParseTreeNode* n)
    {
        // KW_REPEAT stmt-sequence KW_UNTIL exp
        if (n->children.size() != 4 ||
            !isName(n->children[0].get(), "KW_REPEAT") ||
            !isName(n->children[2].get(), "KW_UNTIL")) {
            fail("repeat-stmt 结构异常");
            return;
        }
        std::string lBody = newLabel();
        emit("label", "", "", lBody);
        genNode(n->children[1].get());                 // 循环体
        std::string c = genNode(n->children[3].get()); // until 条件
        emit("jfalse", c, "", lBody);                  // 假 → 跳回 body
    }

    // while（Mini-C）：label cond；cond→t；jfalse t → exit；body；goto cond；label exit
    void genWhile(const ParseTreeNode* n)
    {
        // KW_WHILE ( expression ) statement
        if (n->children.size() != 5) {
            fail("while-stmt 结构异常");
            return;
        }
        std::string lCond = newLabel();
        emit("label", "", "", lCond);
        std::string c = genNode(n->children[2].get());
        std::string lExit = newLabel();
        emit("jfalse", c, "", lExit);
        genNode(n->children[4].get());
        emit("goto", "", "", lCond);
        emit("label", "", "", lExit);
    }

    // for（Mini-C）：init；label cond；cond→t；jfalse t → exit；body；update；goto cond；label exit
    void genFor(const ParseTreeNode* n)
    {
        // KW_FOR ( for-init ; expression ; for-update ) statement
        if (n->children.size() != 9) {
            fail("for-stmt 结构异常");
            return;
        }
        genNode(n->children[2].get());                 // for-init
        std::string lCond = newLabel();
        emit("label", "", "", lCond);
        std::string c = genNode(n->children[4].get()); // 条件
        std::string lExit = newLabel();
        emit("jfalse", c, "", lExit);
        genNode(n->children[8].get());                 // 语句体
        genNode(n->children[6].get());                 // for-update
        emit("goto", "", "", lCond);
        emit("label", "", "", lExit);
    }

    // 赋值语句（TINY assign-stmt）：ID := exp
    void genAssign(const ParseTreeNode* n)
    {
        // ID SYM_ASSIGN exp
        if (n->children.size() != 3 ||
            !isName(n->children[1].get(), "SYM_ASSIGN")) {
            fail("assign-stmt 结构异常");
            return;
        }
        std::string target = n->children[0]->text;
        std::string value = genNode(n->children[2].get());
        emit("=", value, "", target);   // 赋值：arg2 为空，与关系 == 区分
    }

    void genRead(const ParseTreeNode* n)
    {
        // KW_READ ID
        if (n->children.size() != 2) {
            fail("read-stmt 结构异常");
            return;
        }
        emit("read", "", "", n->children[1]->text);
    }

    void genWrite(const ParseTreeNode* n)
    {
        // KW_WRITE exp
        if (n->children.size() != 2) {
            fail("write-stmt 结构异常");
            return;
        }
        std::string v = genNode(n->children[1].get());
        emit("write", v, "", "");
    }

    void genReturn(const ParseTreeNode* n)
    {
        // KW_RETURN SYM_SEMI | KW_RETURN expression SYM_SEMI
        if (n->children.size() == 2) {
            emit("ret", "", "", "");
        } else if (n->children.size() == 3) {
            std::string v = genNode(n->children[1].get());
            emit("ret", v, "", "");
        } else {
            fail("return-stmt 结构异常");
        }
    }

    void genExprStmt(const ParseTreeNode* n)
    {
        // expression SYM_SEMI | SYM_SEMI
        if (n->children.size() >= 2) {
            genNode(n->children[0].get());   // 丢弃表达式值
        }
    }

    // 函数定义（Mini-C）：label <函数名> + 函数体；末尾无显式 ret 时补隐式
    // ret（void 函数 / 无返回路径，设计点 6）。
    void genFunction(const ParseTreeNode* n)
    {
        // type-indicator ID ( parameters ) compound-stmt
        if (n->children.size() != 6) {
            fail("function-definition 结构异常");
            return;
        }
        emit("label", n->children[1]->text, "", "");
        genNode(n->children[5].get());   // compound-stmt
        if (!quads_.empty() && quads_.back().op != "ret") {
            emit("ret", "", "", "");
        }
    }

    void genCompound(const ParseTreeNode* n)
    {
        // { local-definitions statement-list }
        if (n->children.size() != 4) {
            fail("compound-stmt 结构异常");
            return;
        }
        genNode(n->children[1].get());   // local-definitions（无代码）
        genNode(n->children[2].get());   // statement-list
    }

    // ---- 表达式 ----

    // 表达式链：A -> B | A op B（TINY exp/simple-exp/term/power、Mini-C
    // simple-expression/additive-expression/term/power）。children==1 直返子节点
    // 求值；children==3 为 [操作数, op, 操作数]，操作符从中间节点取（设计点 1）。
    std::string genChain(const ParseTreeNode* n)
    {
        if (n->children.size() == 1) {
            return genNode(n->children[0].get());
        }
        if (n->children.size() == 3) {
            std::string lhs = genNode(n->children[0].get());
            std::string op = opText(n->children[1].get());
            std::string rhs = genNode(n->children[2].get());
            std::string t = newTemp();
            emit(op, lhs, rhs, t);
            return t;
        }
        fail("表达式链节点形状异常: " + symName(n));
        return {};
    }

    // 左值描述（T3.2）：普通变量 name；数组变量 name[index]。
    // 只提取地址信息，不读取数组元素；下标表达式本身仍按右值生成。
    struct LValue {
        std::string name;
        std::string index;   // 仅 indexed == true 时有效
        bool indexed = false;
    };

    // Mini-C expression：variable := expression | simple-expression
    // 赋值左侧走左值路径：普通变量发 `=`；数组发 `[]=`，且绝不发 `[]` 读。
    // 赋值表达式返回右值，支持 arr[0] = arr[1] = 1 的右结合链。
    std::string genExpression(const ParseTreeNode* n)
    {
        if (n->children.size() == 1) {
            return genChain(n->children[0].get());
        }
        if (n->children.size() == 3 &&
            isName(n->children[1].get(), "SYM_ASSIGN")) {
            const LValue target = genLValue(n->children[0].get());
            if (failed_) {
                return {};
            }
            const std::string value = genNode(n->children[2].get());
            if (failed_) {
                return {};
            }
            if (target.indexed) {
                // 左值写入：value -> arr[index]
                // ([]=, value, index, arr)
                emit("[]=", value, target.index, target.name);
            } else {
                emit("=", value, "", target.name);   // arg2 为空，与关系 == 区分
            }
            return value;   // 赋值表达式的值 = 右值
        }
        fail("expression 节点形状异常");
        return {};
    }

    // variable 左值：ID | ID [ expression ]
    LValue genLValue(const ParseTreeNode* var)
    {
        LValue lv;
        if (!var || !isName(var, "variable")) {
            fail("赋值左侧不是 variable 节点");
            return lv;
        }
        if (var->children.size() == 1) {
            lv.name = var->children[0]->text;
            return lv;
        }
        if (var->children.size() == 4 &&
            isName(var->children[1].get(), "SYM_LBRACKET") &&
            isName(var->children[3].get(), "SYM_RBRACKET")) {
            lv.name = var->children[0]->text;
            lv.index = genNode(var->children[2].get());
            lv.indexed = true;
            return lv;
        }
        fail("variable 左值节点形状异常");
        return lv;
    }

    // variable（Mini-C）右值读取：ID | ID [ expression ]
    // 数组右值生成 ([] , arr, index, t)，由调用方继续参与运算。
    std::string genVariable(const ParseTreeNode* n)
    {
        if (n->children.size() == 1) {
            return n->children[0]->text;   // ID 名
        }
        if (n->children.size() == 4) {
            std::string idx = genNode(n->children[2].get());
            std::string t = newTemp();
            emit("[]", n->children[0]->text, idx, t);
            return t;
        }
        fail("variable 节点形状异常");
        return {};
    }

    // call（Mini-C）：ID ( arguments )；先 param 后 call
    std::string genCall(const ParseTreeNode* n)
    {
        if (n->children.size() != 4) {
            fail("call 节点形状异常");
            return {};
        }
        genNode(n->children[2].get());   // arguments → param 序列
        std::string t = newTemp();
        emit("call", n->children[0]->text, "", t);
        return t;
    }

    // arguments / argument-list：左递归链，按源顺序逐个 param
    void genArguments(const ParseTreeNode* n)
    {
        for (const auto& c : n->children) {
            if (failed_) {
                return;
            }
            const std::string nm = symName(c.get());
            if (nm == "argument-list") {
                genArguments(c.get());
                continue;
            }
            if (nm == "SYM_COMMA") {
                continue;
            }
            std::string v = genNode(c.get());
            emit("param", v, "", "");
        }
    }

    // expression-list（for-update / 逗号表达式）：左递归链，逐个求值并丢弃
    void genExpressionList(const ParseTreeNode* n)
    {
        for (const auto& c : n->children) {
            if (failed_) {
                return;
            }
            const std::string nm = symName(c.get());
            if (nm == "expression-list") {
                genExpressionList(c.get());
                continue;
            }
            if (nm == "SYM_COMMA") {
                continue;
            }
            genNode(c.get());
        }
    }

    // factor（TINY/Mini-C 同构）：
    //   ( expression ) | variable | call | NUM | ID | [SYM_INC|SYM_DEC] variable
    std::string genFactor(const ParseTreeNode* n)
    {
        if (n->children.size() == 1) {
            const std::string nm = symName(n->children[0].get());
            if (nm == "variable" || nm == "call") {
                return genNode(n->children[0].get());
            }
            return n->children[0]->text;   // NUM / ID（TINY factor -> NUM | ID）
        }
        if (n->children.size() == 3 &&
            isName(n->children[0].get(), "SYM_LPAREN")) {
            // ( expression )：直返内部表达式临时变量，不额外生成四元式（设计点）
            return genNode(n->children[1].get());
        }
        if (n->children.size() == 2) {
            // 前缀 ++/--（当前文法 factor -> SYM_INC variable | SYM_DEC variable）
            // 或后缀 v++/v--（扩展支持）：区分处理——前缀返回自增后值，后缀返回
            // 自增前值（先保存旧值再自增）。
            const bool prefix = isName(n->children[0].get(), "SYM_INC") ||
                                isName(n->children[0].get(), "SYM_DEC");
            const ParseTreeNode* var = prefix ? n->children[1].get()
                                              : n->children[0].get();
            const bool inc = isName(n->children[0].get(), "SYM_INC") ||
                             isName(n->children[1].get(), "SYM_INC");
            const bool dec = isName(n->children[0].get(), "SYM_DEC") ||
                             isName(n->children[1].get(), "SYM_DEC");
            if ((!inc && !dec) || !var || var->children.empty()) {
                fail("factor 自增自减结构异常");
                return {};
            }
            // 复用左值抽象：普通变量直接读写；数组先 [] 读、再 []= 写回，
            // 避免把下标元素误写成临时变量（T3.2）。
            const LValue lv = genLValue(var);
            if (failed_) {
                return {};
            }
            std::string oldValue = lv.name;
            if (lv.indexed) {
                oldValue = newTemp();
                emit("[]", lv.name, lv.index, oldValue);
            }
            std::string t = newTemp();
            emit(inc ? "+" : "-", oldValue, "1", t);
            if (lv.indexed) {
                emit("[]=", t, lv.index, lv.name);
            } else {
                emit("=", t, "", lv.name);
            }
            return prefix ? t : oldValue;   // 前缀=自增后值；后缀=自增前值
        }
        fail("factor 节点形状异常");
        return {};
    }
};

} // namespace

Result<IrProgram> IrGenerator::generate(const Grammar& g, const ParseTree& tree) const
{
    return Gen(g).run(tree);
}

} // namespace compiler::ir
