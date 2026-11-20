# 统一数据结构定义草案（DATA_STRUCTURES）

> 版本：v0.3（T2.5 同步：§5.4 LALR(1) 数据结构契约定稿；T2.1 同步：Symbol 表 `$`/`#` 预置约定、ε 产生式 rhs={} 定稿）
> 日期：2026-10-08
> 位置：`core/include/compiler/core/`，命名空间 `compiler`
> 约定：纯 C++17 + STL，不依赖 Qt；结构体以值语义为主，便于序列化与测试。

## 1. 设计原则

- 所有结构体只描述"数据"，不含算法；算法放在 `regex/`、`lexer/` 等模块。
- `Token` 的 `code` 是语言相关编码，由 `TokenCodeRegistry` 统一分配与管理。
- 语法分析结构（Symbol/Production/Grammar/ParseTreeNode）**本轮只定义，不实现任何算法**。

## 2. 结构总览

```text
TokenCodeRegistry ── 1..N ── TokenType
Token ── code ──► TokenCodeRegistry
Nfa/Dfa 状态 ── 记录 tokenCode + priority（接受态归属哪个单词）
Grammar ── 1..N ── Symbol / Production
ParseTree ── 持有 vector<Token> + 根 ParseTreeNode
Diagnostic / Result ── 所有模块统一错误载体
```

## 3. 词法：Token

```cpp
// core/include/compiler/core/token.h
#pragma once
#include <string>

namespace compiler {

enum class TokenKind : int {
    EndOfFile = 0,   // 文件结束
    Keyword,         // 关键字/保留字
    Identifier,      // 标识符
    Number,          // 数值（整数/浮点）
    String,          // 字符串字面量（扩展用）
    Operator,        // 运算符（:=, +, <=, ...）
    Punct,           // 界符（; , ( ) [ ] ...）
    Comment,         // 注释（仅内部阶段用，不写入 sample.lex）
    Error,           // 非法字符
    Unknown,
};

struct SourceLocation {
    int line   = 1;   // 1 基
    int column = 1;   // 1 基（按字节计，见 FILE_FORMATS.md）
    int offset = 0;   // 文件字节偏移，0 基
};

struct Token {
    int         code   = 0;        // 语言相关单词编码；0=EOF，255=ERROR
    TokenKind   kind   = TokenKind::Unknown;
    std::string name;              // 编码登记名（与 .rules 中 TokenName 一致），如 "KW_IF"、"ID"
    std::string lexeme;            // 源程序中的实际拼写
    SourceLocation loc;

    bool isError() const { return code == 255; }
    bool isEof()   const { return code == 0; }
};

}
```

## 4. 词法：编码登记表

```cpp
// core/include/compiler/core/token_registry.h
#pragma once
#include <string>
#include <vector>
#include <map>
#include "core/token.h"

namespace compiler {

struct TokenType {
    int         code    = 0;
    std::string name;        // 登记名："KW_IF"、"ID"、"NUM"、"SYM_PLUS"...
    TokenKind   kind    = TokenKind::Unknown;
    std::string display;     // 显示/回写形式："if"、"ID"、"$"、"ERROR"
};

// 单词编码登记表：code ↔ name 双向查询，并可序列化为 sample.lex 头部
class TokenCodeRegistry {
public:
    int  registerToken(const TokenType& t);                 // 返回分配的 code
    int  codeOf(const std::string& name) const;             // 找不到返回 -1
    const TokenType* typeOf(int code) const;                // 找不到返回 nullptr
    int  nextCode() const;                                  // 下一个可用编码
    void clear();

    // sample.lex 头部序列化/反序列化（见 FILE_FORMATS.md）
    std::string toHeader() const;
    bool fromHeader(const std::string& text);

    std::vector<const TokenType*> orderedTypes() const;     // 按 code 升序
};

}
```

保留编码约定：`0` = EOF（display `$`），`255` = ERROR（display `ERROR`），均不占用 `.rules` 分配序列。

## 5. 语法：Symbol / Production / Grammar

```cpp
// core/include/compiler/core/symbol.h
#pragma once
#include <string>

namespace compiler {

enum class SymbolKind : int {
    Terminal,      // 终结符
    Nonterminal,   // 非终结符
    EndOfFile,     // 结束符 $（分析表内部用）
    Empty,         // 空串 ε / #（内部用）
};

struct Symbol {
    int  id        = -1;
    std::string name;
    SymbolKind kind = SymbolKind::Terminal;
    int  tokenCode = -1;   // 终结符时：对应词法单词编码；-1 表示无直接对应
};

}
```

```cpp
// core/include/compiler/core/grammar.h
#pragma once
#include <vector>
#include <string>
#include "core/symbol.h"

namespace compiler {

struct Production {
    int  id = -1;
    int  lhs = -1;              // 左部 Symbol.id
    std::vector<int> rhs;       // 右部 Symbol.id 序列（Empty 表示 ε 产生式）
    std::string semantic;       // 可选语义动作占位（任务二/三）
};

struct Grammar {
    std::vector<Symbol>     symbols;
    std::vector<Production> productions;
    int startSymbol = -1;

    const Symbol* symbol(int id) const;                       // 越界返回 nullptr
    int  symbolId(const std::string& name, SymbolKind kind) const;
    std::vector<int> productionsOf(int lhs) const;            // 按出现顺序
    std::vector<int> terminalIds() const;
    std::vector<int> nonterminalIds() const;

    // 任务二算法将挂载在 Grammar 的"结果"侧，不修改本结构：
    // FirstSet / FollowSet / LR0Items / LALR1Items / ACTION / GOTO 等
};

}
```

### 5.1 T2.1 定稿约定（BnfParser 产出）

- **符号表预置两个内部符号**（id 0/1 固定，所有 `BnfParser` 产出的 `Grammar` 一致）：
  - id 0 = `$`，`SymbolKind::EndOfFile`，`tokenCode = 0`（对应保留编码 EOF / display `$`）；
  - id 1 = `#`，`SymbolKind::Empty`，`tokenCode = -1`（ε 的规范记号，供 First 集/可视化引用）。
  - 用户符号从 id 2 起按首次出现顺序分配；`terminalIds()`/`nonterminalIds()` 按 kind 过滤，自动排除这两个内部符号。
- **ε 产生式表示**：`#`/`ε` 候选 → `Production.rhs = {}`（空向量），**不**把 Empty 符号放入 rhs——保证 LR 项 `[A → •]`（dot=0=rhs.size()）即完成态，LR(0)/LALR(1) 构建无需特判。`Grammar.h` 中 rhs 注释「（Empty 表示 ε 产生式）」按此定稿。
- **终结符 → tokenCode**：裸名终结符按 `name` 查 `TokenCodeRegistry`（主 A：读 sample.lex 头部 `fromHeader`；备 B：重解析 `.rules`）；引号字面量按 `display` 匹配，解析后符号名取登记名（如 `'if'` → `KW_IF`）；查不到 → `tokenCode = -1` 且 `BnfParser` 报 `stage="bnf-parse"` 错误。
- **`$` 在 .bnf 右部出现**：引用 id 0 的 EndOfFile 符号（tokenCode 0）。

### 5.2 任务二算法侧结构：First / Follow / Nullable（T2.3）

> 本节记录跨 T2.3→T2.5 的共享契约（LALR lookahead 直接消费）；结构体位于 `src/grammar/FirstFollow.h`
> （命名空间 `compiler::grammar`），**非 core**（core 只放纯数据，见 §1）。

```cpp
struct FirstFollowSets {
    std::vector<std::set<int>> first;      // 按符号 id 索引（含 $=0、#=1；first.size()==symbols.size()）
    std::vector<std::set<int>> follow;     // 按符号 id 索引（仅非终结符有意义；其余恒空）
    std::vector<bool>          nullable;   // 按符号 id 索引
};
```

约定：
- **ε 表示**：ε 在 First 集合中以内部符号 `#`（Empty，id = 1，常量 `kEpsilonSymbolId`）表示；`First(X)` 含 id 1 ⟺ X nullable。Follow 集合不含 ε。
- **内部符号**：`First($) = {$}`（id 0）、`First(#) = {#}`（id 1）、`nullable(#)=true`、`nullable($)=false`；二者不出现在产生式右部，故不进 Follow。
- **Follow(start)**：恒含 `$`（id 0），经 `A->αBβ`（β 可空/空）传播到所有可达非终结符。
- **firstOfString**：`FirstFollow::firstOfString(g, sets, α)` 返回 `FIRST(α)`：逐符号加 `First(Yi)\{ε}`、Yi nullable 则继续，全部 nullable 时末尾加 ε（id 1）；空串 → `{#}`。**ε 只允许在 α 整体可空时进入结果**（否则 B 可空、C 不可空时 `FIRST(B C)` 会错误含 ε）。T2.5 lookahead 用法：`FIRST(β a)` = `firstOfString(g, sets, β ++ [a])`（a 为终结符 lookahead，天然剔除 ε）。

### 5.3 任务二算法侧结构：LR(0) 项目集族（T2.4）

> 跨 T2.4→T2.6/T2.8 共享契约（T2.5 lookahead、T2.6 分析表、T2.8 归约均消费）；
> 结构体位于 `src/parser/Lr0Automaton.h`（命名空间 `compiler::parser`），**非 core**。

```cpp
struct Item { int productionId; int dot; };              // [A -> α·β]，dot ∈ [0, rhs.size()]；operator< 按 (productionId, dot)
struct Lr0State { int id; std::set<Item> items; };       // closure 完备的项目集
struct Lr0AutomatonData {
    std::vector<Lr0State> states;                        // id 从 0 起、BFS 发现序
    std::map<std::pair<int,int>, int> transitions;       // (stateId, symbolId) -> stateId（含 shift 终结符与 goto 非终结符）
    int              augmentedStartSymbolId;             // = g.symbols.size()（不在 g.symbols 内）
    std::string      augmentedStartSymbolName;           // "S'"
    int              augmentedProductionId;              // = g.productions.size()
    std::vector<int> augmentedProductionRhs;             // = {g.startSymbol}
};
```

约定：
- **增广文法**：物化 `S' -> S`，仅存在于 `Lr0AutomatonData` 内部，不写入 Grammar；`S'` 永不出现于任何产生式右部（closure 无需展开它）。接受态 = 含 `[S' -> S·]` 的状态。
- **ε 产生式**：`rhs={}` → 项 `[A->·]`（dot==rhs.size()）即完成态，无出边、不被 goto 消费。
- **状态 id**：从 0 起、BFS 发现序；每个状态都是 closure 完备集。
- **产生式访问路由**：`pid == augmentedProductionId ? augmentedProductionRhs : g.productions[pid].rhs`，防止增广 id 越界访问 `g.productions`。
- **状态数锚点（已验证）**：TINY = 59 / Mini-C = 128（临时脚本推导并已用「平衡括号 `S->(S)S|ε` = 6 态」标准答案自校验；C++ 实现（Lr0Automaton，T2.4）运行复核一致，测试断言精确值）。

### 5.4 任务二算法侧结构：LALR(1) 项目集族（T2.5）

> 跨 T2.5→T2.6/T2.8 共享契约（T2.6 分析表、T2.8 归约均消费）；结构体位于 `src/parser/Lalr1Builder.h`
> （命名空间 `compiler::parser`），**非 core**。路线 B（决策点 1）：先构造规范 LR(1) 项目集族，再同心合并。

```cpp
struct Lr1Item { int productionId; int dot; int lookahead; };  // LR(1) 项：单终结符 lookahead（中间构造用）
                                                               // operator< 按 (productionId, dot, lookahead) 字典序
struct Lr1StateRaw { int id; std::set<Lr1Item> items; };       // 规范 LR(1) 状态（合并前，中间产物）
struct Lalr1Item { int productionId; int dot; std::set<int> lookaheads; };  // 合并后：lookahead 为集合
struct Lalr1State { int id; std::set<Lalr1Item> items; };
struct Lalr1Data {
    std::vector<Lalr1State> states;                        // 合并后状态，id == 对应 LR(0) 状态 id
    std::map<std::pair<int,int>, int> transitions;         // (stateId, symbolId) -> stateId
    std::vector<Lr1StateRaw> intermediateLr1States;        // 规范 LR(1) 中间态（测试「不引入新冲突」/调试用）
    std::map<std::pair<int,int>, int> intermediateLr1Transitions;
    int              augmentedStartSymbolId;               // 透传自 Lr0AutomatonData
    std::string      augmentedStartSymbolName;             // "S'"
    int              augmentedProductionId;
    std::vector<int> augmentedProductionRhs;
};
```

约定：
- **路线 B（决策点 1）**：`closure({[S'->·S, $]})` → LR(1) BFS → 同心合并（LR(0) 核心相同即等价类，lookahead 取并集）。**合并后状态 id 复用 LR(0) 状态 id**，`states.size() == Lr0AutomatonData.states.size()`。
- **lookahead**：单终结符 id 或 `$`（id 0），不含 ε（`firstOfString` 对 `FIRST(β a)` 天然剔除 ε，仍防御剥离 `kEpsilonSymbolId`）。
- **closure 调 firstOfString（不手写）**：对 `[A -> α·Bβ, a]`，`FIRST(β a)` = `FirstFollow::firstOfString(g, sets, β ++ [a])`。
- **中间产物命名**：`intermediateLr1States` / `intermediateLr1Transitions`（与合并后的 `states`/`transitions` 区分，公开成员）。
- **增广信息透传**：从 `Lr0AutomatonData` 拷贝，T2.6 找接受态 `[S'->S·]` 与 S' 符号用。
- **状态数锚点（已推导，临时脚本 %TEMP%\t25_derive 核对）**：TINY 59→309→59（LR(1) 中间 309，测试护栏 [150,500]）；Mini-C 128→408→128（护栏 [300,700]）；合并转移数 == LR(0) 转移数（TINY 158 / Mini-C 441）。
- **冲突实测**：TINY 0/0；Mini-C 悬垂 else sr=1（合并 s115 / LR(1) s397，T2.6 报告）；教科书反例 `S->aAd|bBd|aBe|bAe, A->c, B->c`：LR(1) 0/0 → 合并 rr=2（s6）——**合并只可能新增 R/R，绝不新增 S/R**（测试关键性质）。

### 5.5 任务二算法侧结构：LALR(1) 分析表（T2.6）

> 消费 T2.5 产出 `Lalr1Data`，产出 ACTION/GOTO 表与冲突报告；结构体位于 `src/parser/ParseTable.h`
> （命名空间 `compiler::parser`），**非 core**。决策点 6：冲突一律报告，不静默解决。

```cpp
enum class ActionType : int { Shift, Reduce, Accept, Error };   // Error 为缺省（表中无该格即 Error）
struct Action { ActionType type = ActionType::Error; int target = -1; };
    // target 语义：Shift = 目标状态 id；Reduce = 产生式 id；Accept = -1（忽略）；Error = -1
struct Conflict {
    int state = -1;                 // 冲突所在状态 id
    int terminal = -1;              // 终结符 id（$ = 0）
    std::string type;               // "shift/reduce" | "reduce/reduce"
    std::vector<int> productions;   // 涉及的归约产生式 id（S/R 仅 1 个；R/R 两个按 id 升序）
};
struct ParseTableData {
    std::vector<std::map<int, Action>> action;      // [state][terminalId]（含 $=0）；缺省 Error
    std::vector<std::map<int, int>>    gotoTable;   // [state][nonterminalId]
    std::vector<Conflict> conflicts;                // 按遍历序稳定
    int              augmentedStartSymbolId;        // 透传自 Lalr1Data（T2.8/GUI 复用）
    std::string      augmentedStartSymbolName;      // "S'"
    int              augmentedProductionId;
    std::vector<int> augmentedProductionRhs;
};
```

填表规则（单趟扫描）：
- **GOTO**：`transitions` 中 `(I, A)` 且 A 非终结符 → `gotoTable[I][A] = J`。
- **Shift**：`(I, a)` 且 a 终结符 → `action[I][a] = Shift(J)`（对应 `[A -> α·aβ]` 且 `goto(I,a)=J`）。
- **Reduce**：完成项 `[A -> α·]`（A ≠ S'）对每个 `b ∈ lookaheads` → `action[I][b] = Reduce(pid)`。
- **Accept**：完成项 `[S' -> S·]` 且 lookahead 含 `$` → `action[I][0] = Accept`。

冲突检测（决策点 6）：统一 `writeAction`，同格已被占用即冲突，一律记录、不静默解决：
- **S/R**：已有 Shift 后填 Reduce → `{state, terminal, "shift/reduce", {reducePid}}`，**表内保留 Shift**（标准做法）。
- **R/R**：已有 Reduce(p1) 再填 Reduce(p2)（p1≠p2）→ `{"reduce/reduce", {p1, p2}}`，**表内保留先填（低产生式 id）Reduce**（确定性）。
- 同一 `(pid,dot)` 的 lookahead 已由 T2.5 合并为集合 → 同格同 p 重填幂等跳过，不计冲突。
- 冲突不影响 `Result.ok`（冲突是「报告」而非「失败」）。

锚点（%TEMP%\t26_derive，2026-10-09 已核对）：
- TINY：0 冲突；`GOTO[0][program]=1`（= 接受态）；`ACTION[1][$]=Accept`；Shift 锚 `ACTION[0][KW_IF]=Shift(9)`。
- Mini-C：恰 1 个 S/R：`state=115, terminal=KW_ELSE, productions={31}`（31 为 `condition-stmt -> KW_IF ( exp ) stmt`，测试按 lhs/rhs 反查不硬编码）；`ACTION[115][KW_ELSE]=Shift(118)` 表内保留。
- 教科书反例 `S->aAd|bBd|aBe|bAe, A->c, B->c`：合并后 R/R=2，均在 `state=6`，终端符 `d` 与 `e`，涉及产生式 `{A->c, B->c}`（低 id 在前）。
- 边界：单产生式 `S->a`（3 态 0 冲突）、纯 ε `S->#`（2 态 0 冲突）；空文法/无效输入 → `Result` 失败 `stage="parse-table"`。

### 5.6 任务二算法侧结构：LR 驱动与语法树时序（T2.8）

> 消费 T2.6 产出 `ParseTableData` + token 流 → `Result<ParseTree>`（结构体 `ParseTreeNode`/`ParseTree`
> 已在 §7 定义，**T2.8 不改动**）；驱动位于 `src/parser/LrDriver.{h,cpp}`（命名空间 `compiler::parser`），
> 非 core。决策点 4：进程内，不生成独立解析器；决策点 5 B：遇错即停，只报第一个错误。

**驱动契约**：
- **输入 token 流**：`std::vector<Token>`（可含或不含末尾 EOF）。驱动入口归一化：末 token 非 EOF（或为空）
  时本地追加 EOF token（code=0、kind=EndOfFile、name="EOF"）；兼容 `Scanner.scan`（已含 EOF）与
  `SampleLexReader`（不含 EOF）两源，不引入 `parser → lexer` 依赖。
- **token.code → 终结符 symbolId 映射**：ACTION 键为文法终结符 symbolId，token 用词法编码 code；
  由 `Grammar` 终结符的 `Symbol.tokenCode` 建表（GrammarSimplifier 重编号时保留 tokenCode）。
- **`ParseTree.tokens`**：存**归一化后**的 token 序列（含驱动追加的 EOF）。

**节点构建时序**：
- **Shift → 叶子**：`symbolId = 终结符 id`、`productionId = -1`、`tokenIndex = 归一化 token 流下标`、
  `text = token.lexeme`。EOF 永不 shift（接受由 ACTION[$]==Accept 触发）→ 无叶子引用 EOF。
- **Reduce → 内部节点**：`symbolId = 产生式 lhs`、`productionId = pid`、`tokenIndex = -1`、
  `text = lhs 符号名`。**children 顺序必须与产生式右部一致**：`nodeStack` 连续 `pop_back` 得逆序
  `[D, C, B]`（产生式 `A -> B C D`）→ `std::reverse` → `std::move` 进 `children = [B, C, D]`。
- **ε 产生式（rhsSize=0）**：弹 0 个 state/node，内部节点 children 为空。
- **根节点**：接受时 `nodeStack` 唯一顶层节点 = 开始符号子树（`root->symbolId == g.startSymbol`）。

**错误语义（决策点 5 B，遇错即停）**：`Diagnostic { severity=Error, stage="parse", loc=当前 token.loc }`；
message 含出错 tokenIndex、当前 token、期望终结符集合（`ACTION[top]` keys 的符号名）、当前状态 id。
token.code 无终结符映射时的 message：`无法映射到终结符的 token: code=N lexeme="X"`。
不做 panic-mode 恢复；`Result.ok=false`。

**回环断言口径**：叶子 token 序列（按 tokenIndex 取 `ParseTree.tokens`）== 输入 token 序列**去掉末尾 EOF**。
EOF 为合成 token，不参与比较。

### 5.7 任务二算法侧结构：中间代码（T2.9）

> 消费 T2.8 产出 `ParseTree` + Grammar → `Result<IrProgram>`；位于 `src/ir/IrGenerator.{h,cpp}`
> （命名空间 `compiler::ir`），**非 core**。形式：四元式 (quadruple)；任务书「选做内容」按
> 「选做一律按必做」原则实现。

```cpp
struct Quadruple { std::string op, arg1, arg2, result; };   // 空字段 = ""
struct IrProgram { std::vector<Quadruple> quads; };          // 诊断走 Result<T>
```

**op 白名单（符号式 + 少量助记）**：
- 算术：`+ - * / ^`；一元：`neg`；关系：`== != < <= > >=`（比较 → 布尔临时变量）。
- 赋值：`=`（与关系 `==` 区分：arg2 为空即赋值，打印时 `=` 与 `==` 可视化可区分）；
  下标：`[]` 用于右值读取，`[]=` 用于左值写入（T3.2 定稿）；自增自减：`inc dec`（前后缀语义不同：
  `++v` 返回自增后值，`v++` 先保存旧值再自增、返回旧值）。
- 控制：`label` / `goto` / `jfalse`（条件为假跳转）；I/O：`read` / `write`；函数：`param` / `call` / `ret`。

**临时变量与标签约定**：
- 临时变量 `t1, t2, ...` 单调；标签 `L1, L2, ...` 单调。
- 函数体开头发 `label <函数名>`；void 函数无显式 return 时在函数体末尾补 `ret`（arg1 空）。
- 控制流标签用**回填式**：jfalse/goto 目标未定时先占位，目标 `label` 发出后回填 result 字段。
- `factor -> ( expression )` 直接返回内部表达式临时变量，不额外生成四元式。
- `repeat-until`：cond 为假跳回 body（`jfalse cond → body 首 label`），与 `while` 的
  `jfalse → 出口`方向相反，勿混。

**失败诊断**：`stage = "ir"`（Severity::Error）：防御性未知产生式 / 树结构异常，一律报告。

## 6. 词法自动机：NFA / DFA

```cpp
// core/include/compiler/core/nfa.h
#pragma once
#include <string>
#include <vector>

namespace compiler {

struct NfaEdge {
    enum class Type : int { Epsilon, Char, CharClass, NegatedClass };
    Type type = Type::Epsilon;
    char ch = 0;              // Char 时有效
    std::string charClass;    // CharClass/NegatedClass 时的字符集合（区间已展开）
    int target = -1;
};

struct NfaState {
    int  id = -1;
    bool isAccept = false;
    int  tokenCode = 0;       // 接受态归属的单词编码
    int  priority = 0;        // 规则优先级（.rules 文件顺序，越小越高）
    std::vector<NfaEdge> edges;
};

struct Nfa {
    std::vector<NfaState> states;
    int startState = -1;
};

}
```

```cpp
// core/include/compiler/core/dfa.h
#pragma once
#include <map>
#include <set>
#include <vector>

namespace compiler {

struct DfaState {
    int  id = -1;
    bool isAccept = false;
    int  tokenCode = 0;       // 接受态归属的单词编码
    int  priority = 0;        // 多个 NFA 接受态合并时取最小 priority
    std::map<char, int> transitions;   // 输入字符 → 目标状态；缺省表示无转移（报错）
    std::vector<int> nfaStates;        // 对应 NFA 状态子集（可视化/调试用）
};

struct Dfa {
    std::vector<DfaState> states;
    int startState = -1;
    std::set<char> alphabet;   // 所有参与转移的字符
};

}
```

## 7. 语法树：ParseTreeNode / ParseTree

```cpp
// core/include/compiler/core/parse_tree.h
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "core/token.h"

namespace compiler {

struct ParseTreeNode {
    int  symbolId = -1;        // 本节点符号（终结符或非终结符）
    int  productionId = -1;    // 非终结符展开所用的产生式 id；叶子为 -1
    int  tokenIndex = -1;      // 终结符叶子：所属 Token 数组下标；非叶子为 -1
    std::string text;          // 显示文本（lexeme 或符号名）
    std::vector<std::unique_ptr<ParseTreeNode>> children;

    bool isLeaf() const { return children.empty(); }
};

// 一棵语法树：树持有参与推导的 Token 序列，叶子通过 tokenIndex 引用
struct ParseTree {
    std::vector<Token> tokens;
    std::unique_ptr<ParseTreeNode> root;
};

}
```

说明：叶子节点不复制 `Token` 对象，而是由 `ParseTree` 统一持有 token 数组，避免指针悬挂，也方便可视化时按索引回溯行列号。

## 8. 诊断与结果

```cpp
// core/include/compiler/core/diagnostic.h
#pragma once
#include <string>
#include "core/token.h"

namespace compiler {

enum class Severity : int { Info, Warning, Error };

struct Diagnostic {
    Severity severity = Severity::Error;
    std::string stage;       // 产生阶段："rules-parse" / "nfa-build" / "dfa-build"
                             //             / "dfa-min" / "codegen" / "compile" / "scan"
    std::string message;
    SourceLocation loc;      // 无位置时 line = -1
};

}
```

```cpp
// core/include/compiler/core/result.h
#pragma once
#include <vector>
#include "core/diagnostic.h"

namespace compiler {

template <typename T>
struct Result {
    bool ok = false;
    T    value{};
    std::vector<Diagnostic> diagnostics;

    static Result success(T v) { return {true, std::move(v), {}}; }
    static Result failure(Diagnostic d) { return {false, {}, {d}}; }
    void addDiagnostic(Diagnostic d) { diagnostics.push_back(std::move(d)); }
};

}
```

## 9. 与文件格式的对应

| 数据结构 | 文件格式 | 说明 |
| ---- | ---- | ---- |
| `LexRules` + `TokenCodeRegistry` | `.rules` | 规则解析产物 |
| `Nfa` / `Dfa` | （内存） | 可视化表格由 UI 直接读结构 |
| `Token` 序列 | `sample.lex` | 写出/读回 |
| `Grammar` / `Symbol` / `Production` | `.bnf` | 任务二 BNF 解析产物 |
| `ParseTree` | （内存） | 任务二语法树可视化 |

> 本草案为任务一实现基线；任务二/三如需调整（如 `Grammar` 增加属性），应先在本文档更新并同步 `docs/PROGRESS.md`。
