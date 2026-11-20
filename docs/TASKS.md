# 任务分解与实施计划（TASKS）

> 版本：v0.2
> 日期：2026-10-31
> 当前状态：任务一、任务二已完成；任务三实施中（T3.1 文档已更新，T3.2 起待实现）

## 1. 总览

| 阶段 | 内容 | 状态 |
| ---- | ---- | ---- |
| 任务 0 | 架构与文档（本次） | ✅ 完成 |
| 任务一 | 词法分析（正则→NFA→DFA→最小化→生成→编译→sample.lex） | ✅ 完成（T1.1~T1.10） |
| 任务二 | 语法分析（BNF→化简→First/Follow→LR(0)→LALR(1)→分析表→语法树→IR） | ✅ 完成（T2.1~T2.10） |
| 任务三 | Mini-C 端到端联合测试（真实生成扫描器 → sample.lex → 语法树 → IR） | 🚧 实施中（T3.1~T3.5） |

## 2. 全局硬性要求对照

| # | 要求 | 落实位置 |
| -- | ---- | ---- |
| 1 | C++17 + Qt Widgets + CMake | 顶层 CMakeLists.txt、app/ |
| 2 | 单一应用，Tab/Dock 区分 | MainWindow + QTabWidget |
| 3 | 禁止写死 TINY 逻辑 | .rules/.bnf 语言包驱动（ARCHITECTURE.md D2/D6） |
| 4 | 统一数据结构 | core/（DATA_STRUCTURES.md） |
| 5 | 统一文件格式 | .rules / .bnf / sample.lex（FILE_FORMATS.md） |
| 6 | 生成词法分析源程序可独立编译运行 | codegen + templates，core 无 Qt 依赖 |
| 7 | 主窗口三个任务 Tab | Task1Page/Task2Page/Task3Page（T3.5 重写） |
| 8 | 实现前先更新 docs/ | 本仓库工作流约束 |
| 9 | 任务一/二已完成，任务三按 T3.1~T3.5 实施 | 见 §5 与 ARCHITECTURE.md §8 |

## 3. 任务一（已完成）实施分解

### T1.1 项目骨架
- 顶层 CMakeLists（C++17、Qt6 Widgets、子目录）；`core/regex/lexer/codegen/app` 静态库 + 可执行目标；空 MainWindow 与三个 Tab 占位页。
- 验收：`cmake -S . -B build && cmake --build build` 通过，窗口可启动。

### T1.2 core 数据结构落地
- 按 DATA_STRUCTURES.md 实现全部头文件（含 `core_fwd.h`）；可选单测验证基本语义。
- 验收：`Token`/`Symbol`/`Production`/`Grammar`/`Nfa`/`Dfa`/`ParseTreeNode` 全部可编译。

### T1.3 正则引擎：解析与 Thompson 构造
- `RegexParser`（支持 FILE_FORMATS.md §2.4 子集）；`ThompsonBuilder`（RE→NFA，多规则合并、接受态带 tokenCode/priority）。
- 验收：单测覆盖 字面量/类/反义类/连接/选择/闭包/+/?/转义。

### T1.4 子集构造 NFA→DFA
- ε-closure、move、接受态合并（tokenCode 取同长匹配优先级最小者）。
- 验收：单测比对手工构造的 DFA；输出 DFA 转移表。

### T1.5 DFA 最小化（Hopcroft）
- 以"接受态分组（tokenCode+priority）+ 全字符字母表"初始化划分，反复精化。
- 验收：最小化后状态数 ≤ 手工预期；等价性由样例扫描验证。

### T1.6 词法规则文件解析
- `LexRulesParser`：指令（name/case-sensitive/ignore/comment）+ 规则；`TokenCodeRegistry` 分配编码；生成 `LexLanguageConfig`。
- 验收：正确解析 TINY / Mini-C 规则文件（languages/ 下样例）。

### T1.7 表驱动扫描器（运行时）
- `Scanner`：读源文件 → 按最小 DFA 最长匹配扫描 → 处理注释/空白/非法字符 → 输出 `vector<Token>`。
- 验收：对覆盖全单词的 TINY 样例输出正确 token 流。

### T1.8 代码生成 + 编译运行
- `ScannerSourceGenerator`：把最小 DFA 表序列化为常量数组，渲染 `templates/scanner_main.cpp.in`，输出 `build/generated/<lang>/scanner.cpp`。
- `CompilerRunner`：调用系统编译器生成 `<lang>_scanner`，运行并产出 `sample.lex`。
- 验收：`tiny_scanner test.tiny sample.lex` 生成的文件符合 FILE_FORMATS.md；**生成源码脱离本工程可独立编译**。

### T1.9 任务一 GUI
- Task1Page 完整工作流（步骤按钮 + 一键执行）、NFA/DFA/最小 DFA 表格可视化、sample.lex 预览、编译日志、Diagnostic 汇聚到日志面板。
- 验收：界面可完成任务书示例全流程并展示中间产物。

### T1.10 语言包与端到端验收
- `languages/tiny/{tiny.rules, samples/test.tiny}`、`languages/minic/{minic.rules, samples/test.minic}`。
- 两个样例源程序必须**覆盖该语言全部单词与语句**（任务书要求）。
- 验收：任务书要求的三条路径全部通过，见 ARCHITECTURE.md §9。

### T1.10 正式语言包注意事项（T1.6 测试发现，T1.10 处理，T1.6 不修）

1. **Mini-C 关键字清单需去重**：目标 9 个唯一关键字（else if int float double return void while for）、编码 103–111。T1.6 测试样例的 `Keyword103L` 中 `INT` 出现两次；`Symbol_200L` 中 `=`、`-` 各重复一次——均会触发 `LexRulesParser` 的"名称重复"诊断（该诊断是预期行为）。
2. **Symbol 列表写无空格形式**：`==`、`!=`、`++` 应直接写 `==`/`!=`/`++`，不要写成 `= =`/`! =`/`\+ \+`（后者正则语义等价，但登记名会带空格）。
3. **关键字规则必须排在 ID 规则之前**：否则 ID（正则覆盖关键字 + 优先级更高）会遮蔽关键字。T1.6 修正样例已验证：关键字在前时 `if→103`、`then→104`、`else→105` 正确识别，不被 ID 遮蔽。
4. **命名前缀统一**：用 `KW_`/`Keyword`/`SYM_`/`OP_` 等前缀以便 kind 推断；当前 `LexRulesParser::inferKind` 识别 KW/Keyword/ID/NUM/STR/PUNCT/SYM/OP/SYMBOL 前缀，其余名称 kind=Unknown。若语言包使用其他前缀，需扩展 `inferKind`。
5. **关键字 display 字段**：关键字规则的登记名（`KW_IF`）会写入 sample.lex 头部的 display 字段；若希望显示为拼写（`if`），T1.10 正式语言包需用引号字面量形式 `KW_IF_103 = "IF"`。（T1.8 测试发现，T1.10 处理，T1.8 不修）

## 4. 任务二（已完成：T2.1~T2.10）

- T2.1 `.bnf` 解析器 → Grammar（含终结符→tokenCode 映射）。
- T2.2 文法化简（删除无用、不可达、多余产生式）。
- T2.3 First / Follow 集合。
- T2.4 LR(0) 项目集族（DFA）。
- T2.5 LALR(1)（LR(0) + lookahead 传播 或 LR(1) 合并同心项目）。
- T2.6 LALR(1) 分析表 ACTION/GOTO + 冲突报告。
- T2.7 `SampleLexReader` 读 `sample.lex` 重建 registry 与 token 流。
- T2.8 LR 驱动 + `ParseTreeBuilder` → `ParseTree`。
- T2.9（选做转必做）中间代码。
- T2.10 Task2Page 实现（分析表、语法树可视化；ParserPipeline + --parse CLI）。

### T2.10 验收标准（2026-10-10 定稿）

1. TINY：打开 `tiny.bnf` + 任务一产出的 `sample.lex` → 一键运行 → 6 个子 Tab 全部有内容、日志无 Error；分析表 0 冲突；语法树根 `program`；IR 41 条四元式。
2. Mini-C：LR(0)/LALR(1) 128 态；分析表 1 个 S/R 冲突（s115 / KW_ELSE / Shift(118)）高亮并在冲突清单列出；IR 75 条。
3. 8 个单步按钮各自可独立运行并自动补齐上游；缺前置时报 `[Error] [input]`，不崩溃。
4. `--parse <bnf> <sample.lex> [output-ir]`：stdout 语法树文本、可选写出 IR 文件、退出码 0；失败非 0。
5. 无 sample.lex 时 `runThrough(ParseTable)` 仍 ok（纯降级：tokenCode=-1 + Info）；语法树/IR 需要 sample.lex。
6. 新增 `parser_pipeline_test`（零 Qt）纳入 ctest，全套通过（12/12）。
7. 截图存档：主窗口 + 6 个子 Tab（含冲突高亮），作为实验报告证据。

> T2.1~T2.10 已全部实现（见 PROGRESS.md）；任务二验收锚点：TINY 41 quads / 59 态 / 0 冲突，Mini-C 75 quads / 128 态 / 1 个 S/R（悬挂 else）。

## 5. 任务三（实施中：T3.1~T3.5）

任务三定位：不是第三套算法，而是用 Mini-C 对任务一、任务二做**端到端联合验收**。正式流程必须为：

```text
minic.rules + test.minic
  -> 任务一生成 C++ 扫描器 -> 编译 -> 运行
  -> sample.lex（必须由生成扫描器真实产出）
  -> 任务二 minic.bnf + sample.lex
  -> LALR(1) -> 语法树
  -> 四元式 IR（项目选做转必做）
```

### T3.1 文档与方案定稿（2026-10-31 完成）
- 更新 `ARCHITECTURE.md`：JointPipeline、Task3Page、`--minic`、严格性约束。
- 更新 `TASKS.md`：任务三拆为 T3.1~T3.5。
- 更新 `PROGRESS.md`：记录验收锚点与 4 个决策点。
- 验收：三份文档无“任务三预留/进程内联合”的过期表述。

### T3.2 IrGenerator 数组下标左值
- 修改 `src/ir/IrGenerator.cpp`：支持 `arr[0] = 1;`、`arr[i + 1] = x + y;` 等合法 Mini-C 左值。
- IR 形式（暂定，实施时固化）：读数组沿用 `([], base, index, temp)`；写数组新增 `([]=, base, index, value)`，语义为 `base[index] = value`。复杂下标先求值到临时变量。
- 更新 `test/ir/ir_generator_test.cpp`：新增数组左值用例，断言不失败、生成 `[]=` 四元式、下标表达式正确求值。
- 验收：现有 TINY 41 / Mini-C 75 / 手工例 2 quads 锚点不回归；新增用例通过；`ir_generator_test` ctest 通过。

### T3.3 Mini-C 测试数据扩展
- 扩展 `test/languages/minic/samples/test.minic`：补齐关键字/符号/语句/表达式变体；保持 `main` 最后、函数先定义后使用。
- 新增 `test_edge.minic`：覆盖 `for-init -> variable-definition`（双分号形式并注释）、`for-init -> #`、`for-update -> #`、`expression-list` 逗号、`arguments -> #`、空复合语句、悬挂 else、局部数组、数组左值、`_tmp`/`a_1`/`iffy`/`format` 等 ID 边界。
- 新增 `test_invalid.minic`：非法字符、单独 `!`、缺分号、`3.`、非 ASCII 等负向用例。
- 新增 `test_lex_edge.minic`：词法专项覆盖独立 `.`；`_` 当前作为 ID，报告中说明规格歧义。
- 验收：`--lex` 无 ERROR（`test_invalid` 除外）；`--parse` 语法树根 `program`；`test_edge` 覆盖 36/37/39/40/75；扩展后重新固化 token/quad/状态锚点。

### T3.4 JointPipeline（零 Qt）
- 新增 `src/pipeline/JointPipeline.{h,cpp}`：
  - `JointStage { None, LexRun, Parse, Ir }`（T3.4 固化：IR 作为独立联合阶段，内部走 `ParserPipeline::runThrough(Ir)`）。
  - `JointSnapshot` 聚合 `PipelineSnapshot`、`ParserPipelineSnapshot`、`sampleLexPath`、合并诊断。
  - 严格校验 `runProducedLex == true`；失败即停止，不把预览送入语法分析。
  - 输入变更失效规则：rules/source 变更使 LexRun+Parse 失效；bnf 变更使 Parse 失效。
- 新增 `test/pipeline/joint_pipeline_test.cpp`：fake backend 测编排放行/严格拒绝/诊断合并；真实 Mini-C 端到端由 `--minic` 覆盖。
- 验收：零 Qt 编译；ctest 通过；严格拒绝预览路径有用例断言。

### T3.5 Task3Page + worker + MainWindow + --minic
- 新增 `src/gui/Task3PipelineWorker.{h,cpp}`：QThread 中执行 JointPipeline，回传 `JointSnapshot`，生命周期同 Task1/2。
- 重写 `src/gui/Task3Page.{h,cpp}`：4 个 Tab（源程序+覆盖 / 词法结果 / 语法结果 / 联合报告）；一键联合测试、仅词法、仅语法、导出报告；跳转任务一/二。
- 修改 `src/gui/MainWindow.{h,cpp}`：连接 Task3 日志/诊断/状态信号；去掉 Task3 禁用占位。
- 修改 `src/app/main.cpp`：新增 `--minic <langDir> <source> [outputDir]`，输出 `sample.lex`/tree/IR/report。
- 更新 `src/pipeline`、`src/gui`、`test` 的 CMakeLists。
- 产出测试报告、截图、`sample.lex`/tree/IR 证据；更新 `docs/ARCHITECTURE.md`、`docs/PROGRESS.md`。
- 验收：GUI 一键运行四 Tab 有内容、日志无 Error；`--minic` 退出码 0；ctest 全绿；报告与截图齐全。

### T3 验收标准
1. 词法：Mini-C 9/9 关键字；已定义符号除 `.`/`_` 规格歧义外全覆盖；`test.minic` 0 ERROR。
2. 语法：78 条产生式除已说明的 ε/规格歧义外全覆盖；悬挂 else 正确解析；语法树根 `program`。
3. 联合：`sample.lex` 必须由生成扫描器产出，`runProducedLex == true`；预览回退必须被拒绝。
4. IR：数组下标左值可生成 `[]=`；TINY/Mini-C 既有锚点不回归。
5. 界面：Task3Page 4 个 Tab 有内容；一键联合测试、单步词法/语法、报告导出可用。
6. CLI：`--minic` 端到端成功，产物齐全，失败退出码非 0。
7. 测试：`ctest` 全绿；`joint_pipeline_test`、`ir_generator_test` 新增用例通过。
8. 文档：测试报告、截图、使用说明书、README 齐全，规格歧义与 4 个决策有记录。

## 6. 参考文档问题记录与任务三决策

1. **Mini-C 关键词表 "e1se"**：参考文本写作 `e1se`，依文法实际使用应为 `else`；当前 `minic.rules` 使用 `else`。
2. **Mini-C statement 引用 `dowhile-stmt` 未定义**：第 13 条写 `dowhile-stmt`，但第 16 条定义的是 `while-stmt`；当前 `minic.bnf` 使用 `while-stmt`，无真正 do-while 语句。
3. **TINY 文法 `factor -> (exp)` 使用括号**，但 TINY 单词表未列 `(` `)`：语言包已补 `SYM_LPAREN`/`SYM_RPAREN`。
4. **TINY 的 `{ }` 同时是注释定界符**：`.rules` 以 `@comment block { }` 声明，无需定义 token。
5. **Mini-C 专用符号表中的 `_`（已决策）**：`_` 已作为 ID 首字符；当前语言包不单列 `_` token，单独 `_` 会识别为 ID(101)。保持 `minic.rules` 现状，测试报告说明规格歧义。
6. **Mini-C 专用符号表中的 `.`（已决策）**：`.` 只作为 NUM 的小数点（`3.14`），当前文法没有独立使用 `.` 的产生式；保持现状，另建 `test_lex_edge.minic` 做词法专项，不强行塞进必须通过语法分析的 `test.minic`。
7. **`for-init -> variable-definition` 双分号（已决策）**：`variable-definition` 自带分号，`for-stmt` 又要求一个分号，故合法形式类似 `for (int q; ; q < 1; q = q + 1)`。保留 `minic.bnf`，在 `test_edge.minic` 中加注释说明，不修改文法锚点。
8. **数组下标左值 IR 不支持（T3.2 修复）**：`arr[0] = 1;` 语法可通过，但当前 `IrGenerator` 报“不支持数组下标左值赋值”；T3.2 增加 `[]=` 四元式并补单测。
9. **任务三严格性（已决策）**：联合流水线必须校验 `runProducedLex == true`；任务一编译/运行失败时只显示预览并标红，绝不送入语法分析。
10. 以上均以《编译原理课程设计任务书.pdf》原文为最终依据；规格歧义与决策需在测试报告中留档。
