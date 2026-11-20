# 统一文件格式定义草案（FILE_FORMATS）

> 版本：v0.1（草案）
> 日期：2026-09-18
> 约定：所有文件默认 UTF-8（无 BOM），支持 CRLF / LF 换行；`#` 开头为注释行。

## 1. 格式总览

| 文件 | 扩展名 | 生产者 | 消费者 | 用途 |
| ---- | ---- | ---- | ---- | ---- |
| 词法规则文件 | `.rules` | 手写/编辑器 | 任务一 LexerBuilder | 定义语言的全部单词（正则） |
| 文法文件 | `.bnf` | 手写/编辑器 | 任务二 BNF 解析 | 定义语法（本次仅定格式） |
| 单词编码文件 | `sample.lex` | 任务一生成的扫描器 | 任务二 SampleLexReader | 语法分析输入 |

## 2. 词法规则文件 `.rules`

### 2.1 文件结构（EBNF）

```text
rules-file  := line*
line        := blank | comment | directive | rule
blank       := /\s*/
comment     := '#' ... eol
directive   := '@' name value
rule        := 'rule' TokenName '=' pattern
pattern     := literal-string | regex
```

### 2.2 语言级指令（directive）

| 指令 | 取值 | 默认 | 说明 |
| ---- | ---- | ---- | ---- |
| `@name` | 字符串 | 无 | 语言名，写入 `sample.lex` 头部（**可选**，TINY/Mini-C 语言包可不写；缺省为空） |
| `@case-sensitive` | `true` / `false` | `true` | 是否区分大小写；`false` 时字面量与字符类自动扩展大小写 |
| `@ignore` | 字符类 | `[ \t\r\n]` | 需跳过的空白字符集合 |
| `@comment block` | `<open> <close>` | 无 | 块注释定界符，可多条；不支持嵌套 |
| `@comment line` | `<prefix>` | 无 | 行注释前缀，可多条；遇到换行结束 |

示例：TINY 用 `@comment block { }`；Mini-C 用 `@comment line //`。

### 2.3 规则（rule）

- 行形式（三选一，均兼容）：
  - `rule <TokenName> = <pattern>`（TINY 风格，`rule` 前缀**可省略**）；
  - `<TokenName> = <pattern>`（省略 `rule` 前缀）；
  - `<TokenName><code> = <pattern>` / `<TokenName><code>L = <list>`（Mini-C/任务书风格，显式编码）。
- TokenName 解析（标识符 + 可选编码标记）：
  - 编码标记形式一：`_<digits>`（下划线 + 数字），如 `Comment_102`、`Num_100`、`ID_101`、`Symbol_200L`；
  - 编码标记形式二：`<digits>`（无下划线，仅配合 `L` 使用），如 `Keyword103L`；
  - 无编码标记：按文件出现顺序**自动分配** 1, 2, 3, ...。
  - 显式与自动编码可混用，解析器保证不冲突（`TokenCodeRegistry` 显式编码重载 + 自动分配跳过已用编码）。
- `L`（Launch）后缀：表示"字面量列表"——RHS 是 `|` 分隔的多个字面量，每个字面量自成一个单词，编码从 `<code>` 起递增。
  例：`Symbol_200L= \+ | - | \* | / | = | < | <= | <<` → `+`=200、`-`=201、`*`=202、`/`=203、`=`=204、`<`=205、`<=`=206、`<<`=207。
  列表中 `+`/`*` 等元字符需转义（`\+`、`\*`）。
- 保留编码：`0`(EOF) 与 `255`(ERROR) 保留，`EOF`/`ERROR` 不得用作 TokenName。
- 优先级：文件出现顺序即优先级；`L` 列表内按字面量顺序。扫描采用**最长匹配**；长度相同取先声明者（解决 `<=` vs `<`）。
- 单词类别由 TokenName 前缀决定：
  - `Keyword` 前缀（如 `Keyword103L=INT | DOUBLE | BOOL`）：关键字。**不生成 DFA**（与 ID 规则重合），仅登记编码——INT=103、DOUBLE=104、BOOL=105，由扫描器对 ID 匹配结果重分类。
  - `Comment` 前缀（如 `Comment_102=//(~eoln)*`）：注释。**生成 DFA**（供最长匹配，避免 `//` 被 `/` 吃掉），但输出单词编码文件时**丢弃**。
  - 其余：普通单词，生成 DFA。
- 命名类定义行：`<name> = <pattern>` 且 `<name>` 已在命名类表中（如 `digit=[0-9]`、`letter=[A-Za-z]`）→ 作为命名类（重）定义，**不生成 token**，供后续规则宏展开引用。
- pattern 两种写法：
  - `"..."` 字面量：内部全部按原义字符处理，仅 `\"` 转义引号、`\\` 转义反斜杠。推荐用于关键字与专用符号（如 `":="`、`"{"`）。
  - 裸正则：支持 §2.4 语法。
- 空串：模式内用 `#` 表示；但**词法规则不可接受空串**（会造成扫描死循环），解析/装配阶段校验并报错。

### 2.4 正则语法子集

```text
regex       := alternation
alternation := concatenation ('|' concatenation)*
concatenation := repetition*
repetition  := atom ('*' | '+' | '?')?
atom        := char | class | '.' | '(' regex ')'
class       := '[' '^'? (char | range)* ']'
range       := char '-' char
char        := literal-char | escape
escape      := '\' ( 'n' | 't' | 'r' | '\\' | '\.' | '\-' | '\^' | ... | 'd' | 'w' | 's' | 'D' | 'W' | 'S' )
```

- 元字符（裸正则中需转义）：`| * + ? ( ) [ ] . \ -`；字符类内 `] \ - ^` 需按位置转义。
- 缩写：`\d`=[0-9]，`\w`=[A-Za-z0-9_]，`\s`=[ \t\r\n]，大写为取反。
- 反义类：`[^...]` 表示"非集合内任意字符"。

### 2.5 注释处理约定

- 扫描器在**每个 token 边界**先检测注释定界符：命中块注释则跳到最近的闭定界符之后；命中行注释则跳到行尾（换行符保留，作为空白被 `@ignore` 跳过）。
- 注释与空白一样被跳过，**不输出**为 token。
- 若某定界符同时也是单词（如 Mini-C 的 `{`），仅当 `.rules` 声明为注释时才按注释处理；声明与否由语言包决定。

### 2.6 示例：TINY

```text
@name TINY
@case-sensitive false
@ignore [ \t\r\n]
@comment block { }

# --- 关键字 ---
rule KW_IF     = "if"
rule KW_THEN   = "then"
rule KW_ELSE   = "else"
rule KW_END    = "end"
rule KW_REPEAT = "repeat"
rule KW_UNTIL  = "until"
rule KW_READ   = "read"
rule KW_WRITE  = "write"

# --- 专用符号 ---
rule SYM_PLUS  = "+"
rule SYM_MINUS = "-"
rule SYM_TIMES = "*"
rule SYM_DIV   = "/"
rule SYM_MOD   = "%"
rule SYM_POWER = "^"
rule SYM_LT    = "<"
rule SYM_NE    = "<>"
rule SYM_LE    = "<="
rule SYM_GT    = ">"
rule SYM_GE    = ">="
rule SYM_EQ    = "="
rule SYM_SEMI  = ";"
rule SYM_ASSIGN= ":="
rule SYM_LPAREN= "("
rule SYM_RPAREN= ")"

# --- 标识符与常数 ---
rule ID  = [a-zA-Z][a-zA-Z0-9]*
rule NUM = [0-9]+
```

> 注：参考 TINY 文法 `factor -> (exp)` 使用括号，故语言包补充 `(`/`)` 两个单词；最终以任务书复核为准。

### 2.7 示例：Mini-C

```text
@name MiniC
@case-sensitive true
@ignore [ \t\r\n]
@comment line //

rule KW_ELSE   = "else"
rule KW_IF     = "if"
rule KW_INT    = "int"
rule KW_FLOAT  = "float"
rule KW_DOUBLE = "double"
rule KW_RETURN = "return"
rule KW_VOID   = "void"
rule KW_WHILE  = "while"
rule KW_FOR    = "for"

rule SYM_PLUS    = "+"
rule SYM_MINUS   = "-"
rule SYM_TIMES   = "*"
rule SYM_DIV     = "/"
rule SYM_MOD     = "%"
rule SYM_POWER   = "^"
rule SYM_DOT     = "."
rule SYM_LT      = "<"
rule SYM_LE      = "<="
rule SYM_GT      = ">"
rule SYM_GE      = ">="
rule SYM_EQ      = "=="
rule SYM_NE      = "!="
rule SYM_ASSIGN  = "="
rule SYM_SEMI    = ";"
rule SYM_COMMA   = ","
rule SYM_INC     = "++"
rule SYM_DEC     = "--"
rule SYM_LPAREN  = "("
rule SYM_RPAREN  = ")"
rule SYM_LBRACKET= "["
rule SYM_RBRACKET= "]"
rule SYM_LBRACE  = "{"
rule SYM_RBRACE  = "}"

rule ID  = [_a-zA-Z][_a-zA-Z0-9]*
rule NUM = [0-9]+(\.[0-9]+)?
```

## 3. 文法文件 `.bnf`（任务二输入，本次仅定格式）

### 3.1 文件结构

```text
bnf-file  := line*
line      := blank | comment | directive | production
production:= lhs '->' rhs-alts ';'
rhs-alts  := rhs-alt ('|' rhs-alt)*
rhs-alt   := symbol* | '#' | 'ε'
directive := '@start' nonterminal
```

### 3.2 约定

- 非终结符：裸标识符（如 `stmt-sequence`）。
- 终结符两种写法：
  1. **裸名**：直接匹配词法规则登记名（如 `ID`、`NUM`、`KW_IF`）——推荐，避免二义。
  2. **引号字面量**：`'if'` / `"if"`，先按 `display`（lexeme）查 `TokenCodeRegistry`；查不到则要求 token 的 lexeme 与字面量完全一致。
- `#` 或 `ε` 表示空产生式右部（对应 `SymbolKind::Empty`）。
- `@start <nonterminal>` 声明开始符号；缺省取第一条产生式左部。
- 终结符名称解析失败时，文法加载报错（`Diagnostic`），提示应在 `.rules` 中补充或改为引号字面量。

### 3.3 示例：TINY（片段）

```text
@start program

program        -> stmt-sequence ;
stmt-sequence  -> stmt-sequence SYM_SEMI statement | statement ;
statement      -> if-stmt | repeat-stmt | assign-stmt | read-stmt | write-stmt ;
if-stmt        -> KW_IF exp KW_THEN stmt-sequence KW_END
               |  KW_IF exp KW_THEN stmt-sequence KW_ELSE stmt-sequence KW_END ;
repeat-stmt    -> KW_REPEAT stmt-sequence KW_UNTIL exp ;
assign-stmt    -> ID SYM_ASSIGN exp ;
read-stmt      -> KW_READ ID ;
write-stmt     -> KW_WRITE exp ;
exp            -> simple-exp comparison-op simple-exp | simple-exp ;
comparison-op  -> SYM_LT | SYM_GT | SYM_EQ | SYM_LE | SYM_NE | SYM_GE ;
simple-exp     -> simple-exp addop term | term ;
addop          -> SYM_PLUS | SYM_MINUS ;
term           -> term mulop power | power ;
mulop          -> SYM_TIMES | SYM_DIV | SYM_MOD ;
power          -> factor powop power | factor ;
powop          -> SYM_POWER ;
factor         -> SYM_LPAREN exp SYM_RPAREN | NUM | ID ;
```

### 3.4 示例：Mini-C（片段，演示 `#` 空串）

```text
@start program

program            -> definition-list ;
definition-list    -> definition-list definition | definition ;
definition         -> variable-definition | function-definition ;
variable-definition-> type-indicator ID SYM_SEMI
                   |  type-indicator ID SYM_LBRACKET NUM SYM_RBRACKET SYM_SEMI ;
type-indicator     -> KW_INT | KW_FLOAT | KW_DOUBLE | KW_VOID ;
...
local-definitions  -> local-definitions variable-definition | # ;
statement-list     -> statement-list statement | # ;
...
```

## 4. 单词编码文件 `sample.lex`

### 4.1 文件结构

```text
sample.lex := header* data-line*
header     := '#' 元数据行（含 token 表）
data-line  := code | code '"' lexeme '"' | code lexeme
```

- 数据行以**任务书 PDF 为准**：空格分隔 `编码 [词素]`；无 TAB、无行列号、无 EOF 行。

### 4.2 头部元数据

```text
#file-format: sample.lex
#version: 1
#language: TINY                # 来自 .rules 的 @name
#source: 源文件路径
#generator: CompilerCourse (task1)
#token-count: 24               # 非保留 token 数量
#token 0 EOF $
#token 1 KW_IF if
#token 2 KW_THEN then
...
#token 23 ID ID
#token 24 NUM NUM
#token 255 ERROR ERROR
#token-table-end
```

- `#token <code> <name> <display>`：任务二据此重建 `TokenCodeRegistry`。
- 保留编码：`0`=EOF（display `$`），`255`=ERROR。
- **`#generated-at` 省略**（T1.8 v2 定稿）：宿主 SampleLexWriter 与生成扫描器均不输出时间戳，保证逐字节一致、可复现。

### 4.3 数据行

```text
<code> [<词素>]
```

- 字段间以**空格**分隔；是否带词素由单词类别（`TokenKind`）决定：

  | 类别 | 输出形式 | 示例 |
  | ---- | ---- | ---- |
  | 关键字（`Keyword`） | 只输出编码 | `103` |
  | 标识符（`Identifier`） | 编码 + 带引号词素 | `101 "abc"` |
  | 数字（`Number`） | 编码 + 数值（不带引号） | `100 123` |
  | 符号/界符/运算符（`Operator`/`Punct`） | 只输出编码 | `204` |
  | 注释（`Comment`） | 不输出 | — |

- **不写 EOF 行**（任务书输出样例无 EOF 标记）；EOF 仅存在于内存 token 流 / GUI 调试展示。
- 非法字符输出 `255 <字符>`，并在 stderr 打印错误信息（扫描继续，多错误收集）。
- `line` / `column` 仅用于调试 / GUI 展示，**不写入 sample.lex**；定位按**字节**计数（token 均为 ASCII，行列号对英文源码精确；多字节字符仅出现在被跳过的注释时不影响 token 定位）。

### 4.4 示例

```text
#file-format: sample.lex
#version: 1
#language: TINY
#source: languages/tiny/samples/test.tiny
#generator: CompilerCourse (task1)
#token-count: 24
#token 0 EOF $
#token 1 KW_IF if
#token 2 KW_THEN then
#token 3 KW_ELSE else
#token 4 KW_END end
#token 5 KW_REPEAT repeat
#token 6 KW_UNTIL until
#token 7 KW_READ read
#token 8 KW_WRITE write
#token 9 SYM_PLUS +
#token 10 SYM_MINUS -
#token 11 SYM_TIMES *
#token 12 SYM_DIV /
#token 13 SYM_MOD %
#token 14 SYM_POWER ^
#token 15 SYM_LT <
#token 16 SYM_NE <>
#token 17 SYM_LE <=
#token 18 SYM_GT >
#token 19 SYM_GE >=
#token 20 SYM_EQ =
#token 21 SYM_SEMI ;
#token 22 SYM_ASSIGN :=
#token 23 ID ID
#token 24 NUM NUM
#token 255 ERROR ERROR
#token-table-end
1 2 23 "x" 22 24 123 21
```

（示例对应源文本首行 `if then x := 123 ;`：`if`/`then` 为关键字只输出编码 1、2；`x` 为标识符输出 `23 "x"`；`:=` 为符号只输出 22；`123` 为数字输出 `24 123`；`;` 为符号只输出 21。无 EOF 行、无行列号。）

## 5. 目录与命名约定

- 每个语言一个目录：`languages/<lang>/`，内含 `<lang>.rules`、`<lang>.bnf`（任务二）、`samples/`。
- 生成的扫描器可执行文件命名：`<lang>_scanner`（如 `tiny_scanner`），运行：`<lang>_scanner <源文件> [输出文件]`，缺省输出当前目录 `sample.lex`。
