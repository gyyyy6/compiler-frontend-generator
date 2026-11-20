> **会话恢复摘要**
> - 当前阶段：任务三 **T3.5 实现完成（待评审 / 提交）**（GUI 4 Tab + 联合测试线程 + `--minic` CLI）；T3.4 已提交（ff33bee）
> - 已完成：T1.1~T1.10、T2.1~T2.10、T3.1~T3.4；任务三方案与 4 个决策已定稿（2026-10-31）
> - T3 关键设计：`JointPipeline` 串联 `LexerPipeline → ParserPipeline`；**强制 `runProducedLex == true`**，`false` 时以 stage=`joint-strict` 硬失败，拒绝进程内预览回退
> - T3 决策：①`.`/`_` 保持现状，报告中说明，另建 `test_lex_edge.minic`；②`for-init` 双分号保留，`test_edge.minic` 注释；③修复 `IrGenerator` 数组下标左值；④联合流水线严格校验
> - T3.2 实现：赋值左侧走 genLValue（普通变量/数组变量），数组写 []=、右值读 []；赋值表达式返回右值以支持连续赋值；++/-- 数组复用左值读写；ir_generator_test 39 项 CHECK，ctest 12/12（2026-11-02）
> - T3.3 实现：`test.minic` 扩至 **416 tokens / 0 ERROR / 139 quads**，78 条产生式全覆盖（5 条 ε 无树节点）；新增 `test_edge.minic`（193 tokens / 0 ERROR / 68 quads）、`test_invalid.minic`（46 tokens / 8 ERROR）、`test_lex_edge.minic`（21 tokens / 0 ERROR，独立 `.`→SYM_DOT）；`parser_pipeline_test` 锚点随扩展固化为 139 quads（2026-11-02，ca81a34）
> - T3.4 实现：`src/pipeline/JointPipeline.{h,cpp}`（零 Qt）三段推进 `LexRun → Parse → Ir`，`JointSnapshot` 聚合词法/语法全部产物 + 合并诊断跨线程值传递；`ParserPipeline::reset()` 新增公开复位；`joint_pipeline_test` **138 项 CHECK 全过**，ctest **13/13**，`-Wall -Wextra -Wpedantic` 0 警
> - T3 当前锚点（T3.3 扩展后固化）：见「验收锚点」小节（128 态 / 1 个 S/R / 139 quads / 416 tokens）
> - 下一步：T3.6 收尾 —— 把 `--minic` 产物复制到 `docs/test-reports/artifacts/`、四 Tab 截图、T3.5 提交
> - 分支：develop
> - 注意：语言包位于 `test/languages/<lang>/`；`build/` 不提交，报告证据需复制到 `docs/test-reports/`

# 实施进度（PROGRESS）

> 每次实现前更新本文档，再写代码。

## 当前状态

- 日期：2026-11-02
- 阶段：任务一/二全部完成；**任务三 T3.1~T3.5 实现完成（T3.5 待评审/提交）**
- T3 方案：`JointPipeline`（零 Qt）严格串联 `LexerPipeline → ParserPipeline`；`Task3Page` 4 个 Tab；`--minic` CLI；强制 `runProducedLex == true`
- T3 决策：①`.`/`_` 保持现状，报告中说明，另建 `test_lex_edge.minic`；②`for-init` 双分号保留，`test_edge.minic` 注释；③修复 `IrGenerator` 数组下标左值；④联合流水线严格校验
- T3.3 实现：`test.minic` 416 tokens / 0 ERROR / 139 quads / 78 产生式全覆盖（ε 5 条无树节点：20/22/37/39/75）；`test_edge.minic` 193 tokens / 68 quads；`test_invalid.minic` 46 tokens / 8 ERROR（`@`、单独 `!`、非 ASCII `变量` 6 字节，另含 `3.` 词法合法/语法非法负例）；`test_lex_edge.minic` 21 tokens（`3.14` 单 NUM、`3.`→NUM+SYM_DOT、独立 `.`→SYM_DOT、`_`→ID）
- T3.2 实现：赋值左侧走 genLValue（普通变量/数组变量），数组写 []=、右值读 []；赋值表达式返回右值以支持连续赋值；++/-- 数组复用左值读写；ir_generator_test 39 项 CHECK，ctest 12/12（2026-11-02）
- T2.9 实现要点：操作符从非终结符节点 opText 归一（SYM_EQ→==、SYM_NE→!=）；无 else 的 if 不分配结束标签；repeat-until jfalse 跳回 body、while/for jfalse 跳出口；前缀 ++/-- 返回自增后值、后缀返回自增前值；void/无显式 ret 补隐式 ret；factor->(expression) 直返临时变量
- T2.10 实现要点：顶部操作栏（打开 .bnf / 打开 sample.lex / 一键运行 / 8 个单步按钮）；6 子 Tab（BNF / 化简后文法 / First·Follow / LR(0)+LALR(1) 项目集 / 分析表 / 语法树+IR）；`src/pipeline/ParserPipeline`（零 Qt，ParseStage：LexRead→BnfParse→Simplify→FirstFollow→Lr0→Lalr1→ParseTable→Drive→Ir）；`Task2PipelineWorker`（复用 T1.9 线程模式）；`LrTableView`/`ParseTreeView`（新视图组件，T1.9 组件零改动）；`--parse` CLI 分支复用进程内 ParserPipeline
- T2.6 推导结果（临时脚本 %TEMP%\t26_derive\derive.cpp，2026-10-09 已核对）：TINY 0 冲突、GOTO[0][program]=1=接受态、ACTION[0][KW_IF]=Shift(9)；Mini-C 恰 1 个 S/R（s115/KW_ELSE，prod31，表内保留 Shift(118)）；反例 S->aAd|bBd|aBe|bAe 合并后 rr=2（s6，终端 d/e，A->c vs B->c）；单产生式 S->a（3 态 0 冲突）、纯 ε S->#（2 态 0 冲突）
- T2.5 推导结果（临时脚本 %TEMP%\t25_derive\derive.cpp，已核对）：TINY 59→309→59、Mini-C 128→408→128；合并后状态数与转移数 == LR(0)（TINY 158 / Mini-C 441）；Mini-C 悬垂 else sr=1（LR(1) s397 / 合并 s115，T2.6 报告，合并不新增 S/R）；教科书反例 aAd|bBd|aBe|bAe：LR(1) 0/0 → 合并 rr=2（s6，A->c vs B->c），「合并只可能新增 R/R、绝不新增 S/R」验证通过
- 阻塞：无
- 已知问题：无
- 临时测试文件已清理，项目根目录干净

## T2.9 推导锚点（2026-10-09）

脚本 %TEMP%\t29_derive（易失，已备份到本文档）：
- TINY samples/test.tiny：**41 quads**、t1~t17、L1~L4
- Mini-C samples/test.minic：**75 quads**、t1~t29、L1~L7 + 3 函数 label（add/total/main）
- 手工例 E->E+T|T; T->T*F|F; F->n（n+n*n）：**2 quads**、t1~t2

关键序列：
- TINY：`(<=,x,10,t1)` `(jfalse,t1,,L1)` `(=,1,,y)` `(*,y,2,t2)` `(+,x,t2,t3)` `(/,10,5,t4)` `(%,t4,3,t5)` `(-,t3,t5,t6)` `(=,t6,,z)` `(^,x,2,t7)` `(=,t7,,p)` `(read,,,w)` `(+,w,z,t8)` `(write,t8,,)` `(goto,,,L2)` `(label,,,L1)` `(=,2,,y)` `(!=,y,z,t9)` `(write,t9,,)` `(label,,,L2)` `(label,,,L3)` `(-,x,1,t10)` `(=,t10,,x)` `(write,x,,)` `(<=,x,0,t11)` `(jfalse,t11,,L3)` `(read,,,a)` `(>=,a,5,t12)` `(=,t12,,b)` `(<,a,b,t13)` `(=,t13,,c)` `(>,a,b,t14)` `(=,t14,,d)` `(==,a,b,t15)` `(=,t15,,e)` `(+,a,b,t16)` `(*,t16,2,t17)` `(write,t17,,)` `(jfalse,f,,L4)` `(=,1,,g)` `(label,,,L4)`
- Mini-C：`(label,add,,)` `(+,x,y,t1)` `(ret,t1,,)` `(label,total,,)` `([],values,0,t2)` `(+,t2,n,t3)` `(ret,t3,,)` `(label,main,,)` ... `(label,,,L1)` `(<,i,10,t4)` `(jfalse,t4,,L2)` ... `(goto,,,L1)` `(label,,,L2)` `(label,,,L3)` `(<,i,10,t11)` `(jfalse,t11,,L4)` `(param,sum,,)` `(param,i,,)` `(call,add,,t12)` `(param,arr,,)` `(param,i,,)` `(call,total,,t14)` `(goto,,,L3)` `(label,,,L4)` `(==,sum,100,t17)` `(jfalse,t17,,L7)` `(goto,,,L8)` `(label,,,L7)` `(label,,,L8)` `(+,i,1,t25)` `(=,t25,,i)` `(=,t25,,k)` `(-,i,1,t26)` `(=,t26,,i)` `(=,t26,,k)` `(ret,,,)`
- 手工例：`(*,n,n,t1)` `(+,n,t1,t2)`

设计点（实现须一致）：
1. 操作符在非终结符节点上（addop/relop/comparison-op/mulop/powop），需从两操作数之间取节点并经 opText 归一
2. op 规范映射：SYM_EQ→==、SYM_NE→!=（TINY 的 `=`/`<>` lexeme 归一，避免与赋值 `=` 冲突）
3. 无 else 的 if 不分配结束标签（label_seq == 发出的 label 数）
4. 手工文法用 F 因子化（右操作数为非终结符，与语言包文法同构）
5. repeat-until jfalse 跳回 body；while/for jfalse 跳出口（方向相反）
6. 函数头 `label <函数名>`；void 函数末尾补 `ret`（Mini-C main 隐式 ret）
7. 前缀 ++/-- 返回自增后值（`k=++i` 为 `(+ i 1 t25)` `(= t25 i)` `(= t25 k)`）

> 备注（2026-10-10 实现核对）：上文 Mini-C 关键序列片段中 `(jfalse,t17,,L7)`/`(goto,,,L8)`/`(label,,,L8)`
> 与摘要「L1~L7」及设计点 3 不一致；按摘要与设计点 3 自洽的编号实现并测试固化：
> while L1/L2、for L3/L4、if(无 else) L5、if-else L6/L7，共 7 个控制标签（+3 函数 label）。
> TINY 41 / Mini-C 75 / 手工例 2 quads 锚点全部逐条匹配通过。

## T2.10 架构方案与实施记录（2026-10-10）

方案已审阅通过（9 个决策点全部认可），实施遵循「先更新文档，再按 17 个文件顺序输出代码」。

**LexRead 前置（顺序偏离说明）**：BnfParser 需 registry 做终结符映射，故 ParserPipeline 把 sample.lex 读取放在 BnfParse 之前。阶段序：`LexRead → BnfParse → Simplify → FirstFollow → Lr0 → Lalr1 → ParseTable → Drive → Ir`；其余算法严格按题给顺序执行。

**降级路径**：BnfParser 在 registry=nullptr 时正常工作（裸名/引号字面量 tokenCode=-1，T2.1 已支持）。无 sample.lex 且 `runThrough(ParseTable)` → 纯降级（**不走备 B**，pipeline 不引入 .rules 输入），记 `[Info]` 诊断；`runThrough(Drive/Ir)` → 硬失败（stage="sample-lex-read"）。

**测试锚点**：TINY 41 quads / 59 态 / 0 冲突；Mini-C 75 quads / 128 态 / 1 个 S/R（s115 / KW_ELSE / Shift(118)）；降级路径 `runThrough(ParseTable)` 无 sample.lex 仍 ok。

**文件清单（17 个）**：新增 8（`ParserPipeline.{h,cpp}`、`Task2PipelineWorker.{h,cpp}`、`LrTableView.{h,cpp}`、`ParseTreeView.{h,cpp}`、`parser_pipeline_test.cpp`）；修改 9（`Task2Page.{h,cpp}` 重写、`MainWindow.cpp` 接线、`src/pipeline|src/gui|src/app|test` 的 CMakeLists、`main.cpp --parse`、docs 三件套）。T1.9 组件（`LexerPipeline`/`Task1PipelineWorker`/`AutomatonTableView`/`LogDock`）零改动。

## 任务二实施清单（已完成）

- [x] T2.1 `.bnf` 解析器 → Grammar（2026-10-08 完成：BnfParser.{h,cpp} + 备 B 终结符映射 + 216 项 CHECK + Ctrl+B 通过）
- [x] T2.2 文法化简（删除无用、不可达、多余产生式）（2026-10-08 完成：GrammarSimplifier.{h,cpp} + 107 项 CHECK + Ctrl+B 通过）
- [x] T2.3 First / Follow 集合（2026-10-08 完成：FirstFollow.{h,cpp} + 82 项 CHECK + Ctrl+B 通过）
- [x] T2.4 LR(0) 项目集族（DFA）（2026-10-08 完成：Lr0Automaton.{h,cpp}，TINY=59/Mini-C=128 态，3340 项 CHECK + Ctrl+B 通过）
- [x] T2.5 LALR(1)（LR(1) + 同心合并，决策点 1：路线 B）（2026-10-08 完成：Lalr1Builder.{h,cpp} + lalr1_test，TINY 59→309→59 / Mini-C 128→408→128，14854 项 CHECK + 独立 g++ 0 警告 + CMake 全量构建 + ctest 8/8；Mini-C 悬垂 else sr=1 按锚点断言；待人工 Ctrl+B 复核后提交）
- [x] T2.6 LALR(1) 分析表 ACTION/GOTO + 冲突报告（决策点 6：冲突一律报告）（2026-10-09 完成：ParseTable.{h,cpp} + parse_table_test，1937 项 CHECK + 独立 g++ 0 警告 + CMake 全量构建 + ctest 9/9 + Qt Creator Ctrl+B 通过）
- [x] T2.7 SampleLexReader 读 sample.lex（2026-10-08 完成：SampleLexReader.{h,cpp} + T1→T2 链路，1184 项 CHECK + Ctrl+B 通过；终结符映射主路径 A 可用）
- [x] T2.8 LR 驱动 + ParseTreeBuilder（决策点 4：进程内，不生成独立分析器）（2026-10-09 完成：LrDriver.{h,cpp} + lr_driver_test，154 项 CHECK + 独立 g++ 0 警告 + CMake 全量构建 + ctest 10/10 + Qt Creator Ctrl+B 通过）
- [x] T2.9 中间代码生成（2026-10-10 完成：IrGenerator.{h,cpp}，四元式 + 回填式标签；锚点 TINY 41 / Mini-C 75 / 手工例 2 quads，ir_generator_test 24 项 CHECK + 独立 g++ 0 警告 + CMake 全量构建 + ctest 11/11；待人工 Ctrl+B 复核后提交）
- [x] T2.10 Task2Page 实现（分析表、语法树可视化；ParserPipeline + --parse CLI 复用 LrDriver）（2026-10-10 完成：Qt Creator Ctrl+B 全量构建 + GUI 手工 6 Tab（TINY 0 冲突 / IR 41 四元式与锚点一致）+ ctest 12/12 + --parse CLI 端到端；已提交）

## T3 架构方案与决策（2026-10-31）

### 目标流程

```text
minic.rules + test.minic
  -> 任务一：正则→NFA→DFA→最小DFA→生成扫描器→编译→运行
  -> sample.lex（必须由生成扫描器真实产出）
  -> 任务二：minic.bnf + sample.lex → LALR(1) → 语法树 → 四元式 IR
```

### JointPipeline（T3.4）

- 新增 `src/pipeline/JointPipeline.{h,cpp}`，零 Qt。
- 输入：`rulesText/rulesPath`、`bnfText/bnfPath`、`sourceText/sourcePath`、`outputDir`。
- 阶段：`LexRun`（`LexerPipeline::runThrough(Run)`）→ `Parse`（`ParserPipeline::runThrough(Drive)`，可选 `Ir`）。
- **严格性**：`LexRun` 成功后必须 `snapshot.runProducedLex == true`；否则记 Error 并停止，不把 `previewLexText` 送入 ParserPipeline。
- 快照：聚合 `PipelineSnapshot`、`ParserPipelineSnapshot`、`sampleLexPath`、合并诊断；GUI 经 `Task3PipelineWorker` 跨线程取回。
- 失效规则：rules/source 变更使 LexRun+Parse 失效；bnf 变更使 Parse 失效。

### Task3Page（T3.5）

- 定位：联合测试控制台，不嵌入/不直接复用 Task1Page/Task2Page；复用其 pipeline、`CompilerRunnerBackend`、`populateParseTree()`、`formatIrProgram()`。
- 4 个 Tab：源程序+覆盖检查 / 词法结果（Token 表 + sample.lex）/ 语法结果（语法树 + 冲突 + IR）/ 联合测试报告。
- 顶部：打开语言包、打开/保存源程序、一键联合测试、仅词法、仅语法、导出报告；提供跳转任务一/二按钮。
- 严格显示：真实生成扫描器产物标绿；预览回退标红且不进入语法分析；IR 作为独立阶段状态。

### 4 个决策点（2026-10-31 确认）

1. **`.` 和 `_`**：保持 `minic.rules` 现状，不改规则；报告中作为规格歧义说明；可建 `test_lex_edge.minic` 做词法专项（独立 `.`；`_` 当前为 ID）。
2. **`for-init` 双分号**：保留 `minic.bnf`；合法形式类似 `for (int q; ; q < 1; q = q + 1)`；在 `test_edge.minic` 中加注释说明。
3. **数组下标左值**：必须修 `IrGenerator`，让 `arr[0] = 1;` 正常生成 IR；T3.2 增加 `[]=` 四元式并补 `ir_generator_test`。
4. **联合流水线严格性**：必须校验 `runProducedLex == true`，拒绝预览回退。

### 验收锚点（T3.3 扩展后重新固化，2026-11-02）

- `test.minic`：**416 tokens**、0 ERROR、35 个非 EOF/ERROR 登记项、78 条产生式**全覆盖**、128 LR(0)/LALR(1) 态、1 个 S/R（s115/KW_ELSE）、语法树根 `program`、**139 quads**。
- `test_edge.minic`：193 tokens、0 ERROR、68 quads、语法树根 `program`（树中可见 50 条产生式，全部包含在 `test.minic` 的覆盖集内）。
- `test_lex_edge.minic`：21 tokens、0 ERROR；`3.14`→单 NUM、`3.`→NUM+SYM_DOT、独立 `.`→SYM_DOT(223)（共 2 个）、`_`→ID(101)。
- `test_invalid.minic`：46 tokens、**8 ERROR**（`@` 1 + 单独 `!` 1 + 非 ASCII `变量` 6 字节逐字节恢复）；`3.` 词法合法（NUM+SYM_DOT）而语法非法；首个语法错误在第 4 行 `int missing_semi`（缺分号）处遇错即停。
- 产生式覆盖说明：**78 条全部可达**；其中 5 条 ε 产生式（20 `local-definitions->#`、22 `statement-list->#`、37 `for-init->#`、39 `for-update->#`、75 `arguments->#`）结构上已走到，但 ε 归约不产生树节点，故树中看不到 `[prod N]`。
- 词法缺口（规格歧义，已决策）：`SYM_DOT(223)` 已作为独立 token 存在，但 `3.` 仍按「NUM 后接 DOT」切分；单独 `_` 归为 ID(101)。
- 扩展后 `--lex` 进程内产出 `sample.lex` 3363 字节；生成扫描器输出与其逐字节一致（扩展前 2329 字节已核，扩展后的逐字节比对在 T3.5 `--minic` 阶段复核并归档证据）。

### 计划文件

- 新增：`src/pipeline/JointPipeline.{h,cpp}`、`src/gui/Task3PipelineWorker.{h,cpp}`、`test/pipeline/joint_pipeline_test.cpp`、`test_edge.minic`、`test_invalid.minic`、`test_lex_edge.minic`、测试报告/截图/证据。
- 修改：`src/gui/Task3Page.{h,cpp}`（重写）、`src/ir/IrGenerator.cpp`、`test/ir/ir_generator_test.cpp`、`src/app/main.cpp`（`--minic`）、`MainWindow`、各 CMakeLists、三份 docs。

## T3.3 实施记录（2026-11-02，commit ca81a34）

- `test.minic` 扩展：补齐 9 关键字、23 符号、6 类语句、前后缀自增、数组（声明/读/写/下标表达式）、空参数 `void`、多参数调用、悬挂 else、嵌套控制流、`_tmp`/`a_1`/`iffy`/`format` 等 ID 边界。
- 新增 `test_edge.minic`：`for-init -> variable-definition`（双分号形式 + 注释）、`for-init -> #`、`for-update -> #`、`expression-list` 逗号、`arguments -> #`、空复合语句、悬挂 else、局部数组、数组左值。
- 新增 `test_invalid.minic`：非法字符 `@`、单独 `!`、**非 ASCII `变量`（6 字节 → 6 ERROR）**、`3.`（词法合法、语法非法）、缺分号；合计 8 ERROR。
- 新增 `test_lex_edge.minic`：独立 `.`、`3.`、`3.14`、`_` 词法专项。
- 锚点复算：`test.minic` 234→**416 tokens**、75→**139 quads**；`parser_pipeline_test` / `ir_generator_test` 同步；78 条产生式全覆盖（ε 5 条无树节点）。

## T3.4 实施方案（JointPipeline，2026-11-02）

### 文件与职责

- 新增 `src/pipeline/JointPipeline.{h,cpp}`（零 Qt，仅依赖 `LexerPipeline` + `ParserPipeline`）。
- `JointStage { None, LexRun, Parse, Ir }`：三段推进，内部仍按子流水线的细粒度阶段逐个执行——
  `Rules→Nfa→Dfa→Minimize→Codegen→Compile→Run` 后进入 `LexRead→BnfParse→Simplify→FirstFollow→Lr0→Lalr1→ParseTable→Drive→Ir`。
- `JointSnapshot`：`lex`（`PipelineSnapshot`）+ `parse`（`ParserPipelineSnapshot`）+ `sampleLexPath` + `runProducedLex` + `lexStage`/`parseStage`/`validThrough` + 合并诊断；全部值语义（`ParseTree` 用 `shared_ptr`），可跨线程拷贝回 GUI。

### 绝对强制校验（决策点 4）

- `runStageLexRun()` 在 `LexerPipeline::runThrough(Run)` 之后**必须**读取 `snapshot().runProducedLex`：
  - `false`（编译/运行失败回退到进程内预览）→ 记 `Severity::Error` + `stage="joint-strict"`，**立即返回失败**，`currentStage_` 停在 `None/LexRun 之前`；
  - `true` → 才把 `runLexText` 与 `sampleLexPath` 注入 `ParserPipeline`（`setSampleLexText`/`setSampleLexPath`）。
- 任何 `Parse`/`Ir` 阶段前置检查 `strictLexOk_`；未通过直接失败，语法流水线**完全不被调用**（测试断言其快照为空）。
- 校验为双保险：既看 `runThrough` 返回值，也独立复核 `runProducedLex`。

### 接口

```cpp
JointPipeline p;                       // 构造需注入 CompileRunBackend&
p.setRulesText / setRulesPath / setSourceText / setSourcePath / setOutputDir;
p.setBnfText / setBnfPath;
p.loadInputs(rulesPath, bnfPath, sourcePath);   // 便捷入口（CLI/GUI 共用，UTF-8 去 BOM）
p.setStopCallback / setStageCallback / setLexStageCallback / setParseStageCallback;
p.runThrough(JointStage::Ir);          // 分段推进
p.runLexerThrough(Stage::Minimize);    // 词法单步
p.runParserThrough(ParseStage::Lr0);   // 语法单步（需严格词法已通过）
JointSnapshot s = p.snapshot();
```

### 失效规则

- `setRulesText` / `setSourceText` / `setSourcePath` / `setOutputDir` → `LexRun` 起全部失效（并转发给 `LexerPipeline`）。
- `setBnfText` → 仅 `Parse`（含 `Ir`）失效（转发给 `ParserPipeline`，`LexRead` 保留）。
- 失效时 `strictLexOk_` 复位 `false`。

### 测试（`joint_pipeline_test.cpp`，零 Qt + fake `CompileRunBackend`）

1. **真实端到端**：fake backend 把进程内扫描得到的 `sample.lex` 文本落到 `outPath`（模拟生成扫描器真实产出）→ `runThrough(Ir)` 成功，`runProducedLex==true`、`sampleLexPath` 存在、树根 `program`、128 态、1 个 S/R、139 quads。
2. **强拒预览**：`failCompile` / `failRun` 两种回退 → `joint-strict` Error，`Parse` 未被调用（`parse.tokens` 空、`parseTree` 空），`validThrough == None`。
3. **失效**：改 `bnf` 回退到 `LexRun`；改 `source` 回退到 `None`。
4. **单步推进**：`runLexerThrough` 逐阶段 → `runParserThrough` 逐阶段；`lexStage`/`parseStage` 对齐。
5. **诊断合并**：快照同时含词法（`rules-parse`/`dfa-build`/`run`）与语法（`bnf-parse`/`lr-driver`）阶段名。
6. **loadInputs**：成功 + 缺文件失败诊断。

### 实施结果（2026-11-02）

- 新增 5 个文件：`src/pipeline/JointPipeline.h`、`src/pipeline/JointPipeline.cpp`、`test/pipeline/joint_pipeline_test.cpp`；修改 4 个：`src/pipeline/ParserPipeline.{h,cpp}`（新增公开 `reset()`）、`src/pipeline/CMakeLists.txt`（TASK2 分支加入 JointPipeline 源文件）、`test/CMakeLists.txt`（注册 `joint_pipeline_test`）。
- `ParserPipeline::reset()`：`invalidateFrom(LexRead)` + 清诊断 + `lastRunOk_=false`。词法输入（rules/source/outputDir）变更时由 `JointPipeline` 调用，防止上一轮语法树/分析表被当成新结果。
- 诊断合并修正：子流水线每次 `runThrough` 都会 `diags_.clear()`，因此联合层改为**按阶段累积**（`lexDiags_`/`parseDiags_`），快照顺序固定「词法 → 语法 → 联合」；`setSourcePath` 的文件读取失败诊断（stage=`input`）也并入累积视图。
- 严格校验实测：编译失败（`validThrough=Codegen`）与运行失败（`validThrough=Compile`）两条路径均报 `joint-strict` Error，且 `parse.tokens/parseTree/grammar/ir` 全空 —— 证明预览从未进入语法分析。
- 验证：`joint_pipeline_test` **138 项 CHECK 全过**（① 全链路 416 tokens/128 态/1 个 S/R/139 quads；② 两条拒绝路径；③ 前置拒绝；④ bnf/source 失效；⑤ 单步逐阶段；⑥ 诊断合并；⑦ loadInputs 缺失 + 停表）；`g++ -Wall -Wextra -Wpedantic -fsyntax-only` 0 错 0 警（零 Qt，无需任何 Qt include）；`cmake --build` 全量 0 错 0 警；`ctest` **13/13**（`skeleton_smoke` 需 Qt DLL 在 PATH，属既有环境约束）。

## T3.5 实施方案与实施结果（Task3Page + Worker + `--minic`，2026-11-02）

### 目标与文件顺序

一次实现任务三的 GUI 控制台与命令行入口，按下列顺序逐文件输出（每个文件评审后再继续）：

1. `src/gui/Task3PipelineWorker.h` 2. `.cpp` 3. `src/gui/Task3Page.h` 4. `.cpp`
5. `src/gui/MainWindow.{h,cpp}`（接线 + 去掉任务三禁用）6. `src/app/main.cpp`（`--minic`）7. `src/gui/CMakeLists.txt` + `src/app/CMakeLists.txt`

### 复用清单（不闭门造车）

| 能力 | 复用来源 |
| --- | --- |
| 语法树展示 | `gui::populateParseTree(tree, ParseTree&, QLabel*)`（T2.10，`ParseTreeView`） |
| 四元式文本 | `pipeline::formatIrProgram(const ir::IrProgram&)` |
| 语法树文本（报告/CLI） | `pipeline::formatParseTree(const ParseTree&)` |
| 编译/运行 | `gui::CompilerRunnerBackend`（包 `LexerCompilerRunner`，注入 `JointPipeline`） |
| 流水线 | `pipeline::JointPipeline`（T3.4，零 Qt；严格 `runProducedLex`） |
| 阶段名 | `pipeline::jointStageName / lexStageName / parseStageName` |
| 文法统计 | `snapshot.parse` 的 `simplifiedGrammar / lr0 / lalr1 / parseTable` 只读访问 |

### 关键决策

1. **报告与覆盖渲染位置**：`JointPipeline`（T3.4）只提供阶段名与 `formatParseTree/formatIrProgram`，没有报告渲染；T3.4 已提交，本步不修改。为保证 GUI「导出报告」与 `--minic` 输出**同一份** Markdown，把纯函数
   `computeJointCoverage()` / `formatJointTimeline()` / `formatJointReportMarkdown()` 放在 `Task3PipelineWorker.{h,cpp}`（`compiler::gui` 命名空间，签名只用 STL 类型），内部直接调用上述 `pipeline` 格式化函数。
2. **线程模型**：`Task3PipelineWorker` 持有 `CompilerRunnerBackend backend_` 与 `JointPipeline joint_`（构造时注入 backend）。槽 `runTask(int mode, int stage, 输入 5 项, 输出目录)`：mode `0=Joint`（强制 `runThrough(JointStage)`）、`1=Lexer`（`runLexerThrough(Stage)`）、`2=Parser`（`runParserThrough(ParseStage)`）。结果 `finished(bool ok, JointSnapshot, qint64 elapsedMs)`；关闭页面时 `requestStop + quit + wait`（同 T1.9/T2.10）。
3. **单步状态延续**：同一 worker 内的 `JointPipeline` 实例在「仅词法」→「仅语法」之间复用；输入文本未变时 `setRulesText/setSourceText/setBnfText` 不失效（子流水线已按内容比较做短路），因此语法单步能命中上一轮真实产出的 `sample.lex`。
4. **源程序注入**：GUI 统一用 `setSourceText`（由 `LexerPipeline::ensureSourceFile` 落盘 `<outputDir>/<lang>/_input_src.txt`），**不使用 `setSourcePath`**——该接口在「先读文件、后改文本」时保留旧路径，会让扫描器继续读旧文件。`sourceDirty_`（`textChanged` 置位、打开/保存清位）仅用于报告标注与保存提示。
5. **覆盖矩阵（Tab1）**：三类，全部来自联合快照，不重跑流水线——
   - 关键字：`simplifiedGrammar` 中 `KW_` 前缀终结符 × `parse.tokens` 的 `name` 命中；
   - 符号：`SYM_` 前缀终结符 × token 命中；
   - 产生式：`parseTree` 遍历收集 `productionId`，对照 `simplifiedGrammar.productions`；5 条 ε 产生式（20/22/37/39/75）不产生树节点，单独标注「ε，无树节点」而非误判未覆盖。
   未覆盖项在表格中红底；汇总显示 `已覆盖/总数（百分比）`。
6. **4 个 Tab**：①源程序（编辑器 + 覆盖矩阵）②词法结果（Token 表 code/name/kind/lexeme/行列 + `sample.lex` 预览 + 统计/扫描器路径；`runProducedLex==false` 时红字标注「进程内预览，未进入语法分析」）③语法结果（`populateParseTree` + 状态数/冲突 + `formatIrProgram`）④联合报告（Markdown 只读 + 导出）。
7. **跳转**：`Task3Page` 发 `requestTask1()`/`requestTask2()`，`MainWindow` 切到对应 Tab；不复制其内部状态。
8. **`--minic` 约定**：`--minic <langDir> <source> [outputDir]`；`<langDir>` 下按目录名找 `<lang>.rules`/`<lang>.bnf`（找不到则取目录内首个 `*.rules`/`*.bnf`）。产物：`sample.lex`（流水线写入 `outputDir/<lang>/`）、`<source 文件名>.tree.txt`（如 `test.minic.tree.txt`）、`.ir.txt`、`.report.md`。退出码：`0` 全链成功；`2` 用法错误；`3` 词法/语法（必需阶段）失败；`4` I/O 失败；`5` 词法+语法成功但 IR 失败（报告标注）。

### 验收锚点

- GUI：加载 `test/languages/minic` + `samples/test.minic`，「一键联合测试」显示 416 tokens / 0 ERROR / 128 态 / 1 个 S/R / 139 quads；四个 Tab 均有内容；导出报告含阶段时间线、覆盖矩阵、诊断汇总。
- CLI：`--minic test/languages/minic test/languages/minic/samples/test.minic`（outputDir 缺省 `build/generated`）退出码 0，四个产物齐全；`test_invalid.minic` 退出码 3 且报告含 8 个词法 ERROR。
- 构建：`cmake --build` 0 错 0 警；`ctest` 保持 13/13。

### 实施结果（2026-11-02）

**文件**：新增 `src/gui/Task3PipelineWorker.{h,cpp}`；重写 `src/gui/Task3Page.{h,cpp}`（原为整体禁用占位）；修改 `src/gui/MainWindow.{h,cpp}`（接线 4 类信号 + `goToTask1/goToTask2` 跳转）、`src/app/main.cpp`（`--minic`）、`src/gui/CMakeLists.txt`（TASK2 分支加入 worker）、`src/app/CMakeLists.txt`（注释说明依赖来源）。

**Worker**：
- `JointRunMode{Joint, Lexer, Parser}` + 槽 `runTask(mode, stage, rules/bnf/source 文本, outputDir)`；同一 `JointPipeline` 实例贯穿多次单步，故「仅词法」→「仅语法」可续跑。
- 报告/覆盖纯函数：`computeJointCoverage` / `coverageSummary` / `jointTokens` / `formatJointTimeline` / `formatJointReportMarkdown`；直接复用 `pipeline::formatParseTree / formatIrProgram / jointStageName`。
- `jointTokens`：优先 `parse.tokens`；仅词法运行时用 `SampleLexReader` 解析 `lex.runLexText`（仅供展示/统计）。

**GUI**：
- 操作栏 3 行：打开语言包 / 打开·保存源程序 / 一键联合测试 / 仅词法 / 仅语法 / 导出报告；跳转任务一·任务二；语言包·源程序·输出路径 + 耗时 + 状态。
- 4 Tab：源程序（编辑器 + 关键字/符号/产生式覆盖矩阵，未覆盖标红）+ 词法结果（Token 表 + sample.lex + 真实/预览徽标）+ 语法结果（`populateParseTree` + 状态/冲突统计 + `formatIrProgram`）+ 联合报告（Markdown 只读，可导出）。
- 「一键联合测试」强制走 `JointPipeline::runThrough(Ir)`；「仅语法测试」前置 `strictLexOk` 检查（同 worker 内上一轮真实产物）。
- 预览回退时徽标红字「进程内预览（未进入语法分析）」，与 T3.4 严格性一致。

**CLI**：`--minic` 分支构造 `QCoreApplication`（`QProcess` 需要），`loadInputs` → `runThrough(Parse)` → `runThrough(Ir)`；失败也写报告；stdout 打印 `[joint]` 摘要与 `[artifact]` 路径。

**验证**：
- `cmake --build build -j` 全量 **0 错 0 警**；三个改动 TU 以 `-Wall -Wextra -Wpedantic -fsyntax-only` 复核 **0 诊断**。
- `ctest` **13/13**（需 Qt6/mingw DLL 在 PATH）。
- `--minic test/languages/minic samples/test.minic`：**exit 0**，`416 tokens / 128 态 / 1 冲突 / 139 quads / runProducedLex=true`，报告 2157 行；覆盖 **关键字 9/9、符号 23/23、产生式 73/73（+5 ε）**。
- `--minic ... samples/test_invalid.minic`：**exit 3**，46 tokens，时间线第 15 行 `✗ 失败`、第 16 行 `— 未执行`，仅写 sample.lex + report。
- GUI `QT_QPA_PLATFORM=offscreen` 启动 6 s 无崩溃（窗口构造与 worker 线程正常）。

**已知限制**：`sample.lex` 不含源程序行列号，Tab2/报告的 Token 表列「顺序」为数据区序号（1 基），不伪造行列；四 Tab 截图与产物归档留待 T3.6。

## 下一步（任务三）

- [x] T3.1 文档与方案定稿（2026-10-31 完成：ARCHITECTURE/TASKS/PROGRESS 更新；4 个决策记录）
- [x] T3.2 IrGenerator 数组下标左值（2026-11-02 完成：`genLValue` + `[]=` 写回、`[]` 右值读取；覆盖 `arr[0]=1`、`a[i]=b+c`、`x=arr[i]+1`、`arr[0]=arr[1]=1`、`k=++arr[i]`（手工树 + 端到端）；39 项 CHECK + ctest 12/12）
- [x] T3.3 Mini-C 测试数据扩展（2026-11-02 完成，commit ca81a34：`test.minic` 416 tokens/139 quads、`test_edge.minic` 193 tokens/68 quads、`test_invalid.minic` 8 ERROR（含非 ASCII 与 `3.`）、`test_lex_edge.minic` 21 tokens；78 产生式全覆盖）
- [x] T3.4 JointPipeline（零 Qt）+ `joint_pipeline_test`（2026-11-02 完成：三段推进 LexRun→Parse→Ir、`JointSnapshot` 值语义跨线程、`runProducedLex==false` 即 `joint-strict` 硬失败且语法流水线零调用；138 项 CHECK + ctest 13/13）
- [x] T3.5 Task3Page + `Task3PipelineWorker` + MainWindow + `--minic`（2026-11-02 完成：4 Tab、一键联合/单步、覆盖矩阵、Markdown 报告导出、跳转；`--minic` 退出码 0/3 双路径验证；ctest 13/13；截图/产物归档留待 T3.6）

## 已完成

- [x] 通读参考语料：TINY 单词/文法、Mini-C 单词/文法（GBK 文本）
- [x] `docs/ARCHITECTURE.md` —— 模块划分、目录结构、主窗口方案、预留接口
- [x] `docs/DATA_STRUCTURES.md` —— 统一数据结构草案
- [x] `docs/FILE_FORMATS.md` —— .rules / .bnf / sample.lex 统一格式草案
- [x] `docs/TASKS.md` —— 三任务分解与验收
- [x] `docs/PROGRESS.md` —— 本文档
- [x] **版本控制初始化**（2026-09-21 完成）：`git init`、日常开发分支 `develop`、`.gitignore` 已配置（排除 `build/`、`.qtcreator/`、`*.exe` 等构建产物）。
- [x] **T1.1 项目骨架**（2026-09-19 完成）：主窗口成功运行（三 Tab、菜单栏、日志 Dock）。
- [x] **T1.2 core 数据结构**（2026-09-19 完成）：core 数据结构全部头文件已创建（Token.h、TokenCodeRegistry.h、Symbol.h、Grammar.h、NFA.h、DFA.h、ParseTree.h、Diagnostic.h、Result.h），项目 Ctrl+B 编译通过。
- [x] **T1.3 正则引擎**（2026-09-19 完成）：RegexAst.h、RegexParser.{h,cpp}（转义/连接/|/*/+/?/[]/()/~/#、命名展开 digit/letter、字符类外空白作分隔符）、RegexToNfa.{h,cpp}（Thompson 构造）、NfaToDfa.h（T1.4 接口预留）；compiler_regex 改 STATIC，Qt Creator Ctrl+B 编译通过，NFA 语言成员模拟测试通过。
- [x] **T1.4 NFA→DFA 子集构造**（2026-09-19 完成）：NfaToDfa.cpp 实现子集构造（ε-闭包、单字符 move、接受态 tokenCode/priority 合并、字母表收集），对空 NFA / 非法 startState / 越界边目标返回 `stage="dfa-build"` 诊断而非越界崩溃；测试证据：15 个模式 × 1111 串 = 16665 次比较全部一致，接受态合并正确（token=2 / pri=1），`g++ -O2 -Wall -Wextra` 编译 0 错误 0 警告、运行退出码 0、无崩溃无弹窗；NfaToDfa.cpp 已纳入 compiler_regex 构建（src/regex/CMakeLists.txt），Qt Creator 编译通过。
- [x] **T1.5 DFA 最小化**（2026-09-21 完成）：MinimizeDfa.{h,cpp} 实现 Hopcroft 算法——初始划分按接受态 (tokenCode, priority) 签名分组（保证不同 token/priority 的等价终态不合并）、逆转移表 + 时间戳标记精化、块分裂较大者留原块较小者入队、按块最小状态 id 确定性编号；保留 startState 与 alphabet，合并 nfaStates 供可视化；接受态为空直接返回原 DFA；死状态（trap）按普通状态参与划分；失败诊断 `stage="dfa-minimize"`；已纳入 compiler_regex 构建（src/regex/CMakeLists.txt）。测试证据：单/多规则正则 + 4 组手建 DFA + 失败诊断用例，共 **32632 次 CHECK 全部通过**——最小化前后经 DFA 直接模拟逐串对比，接受语言与 tokenCode/priority 完全一致；期望状态数全部符合（a*:2→1、a|b:3→2、(ab)*:3→2、a(b|c)*d:5→3、abc|de:6→5、keyword+id+num:9→9、a*/a+ 优先级合并:2→1 等；同签名等价终态合并 3→2、异 tokenCode/异 priority 不合并各 3）；`g++ -O2 -Wall -Wextra` 编译 0 错误 0 警告、退出码 0；测试程序编译运行于系统 temp 目录，未污染项目根目录与 build 目录。
- [x] **T1.7 表驱动扫描器**（2026-09-30 提交 b0edf25）：新增 `src/lexer/Scanner.{h,cpp}`；`configure(spec, minDfa)` 一次性预处理 `trans_[state][256]` 密集表 + `acceptCode_[state]` + `commentCodes_`；`scan(text, path)` 可重复调用，最长匹配（记录 lastAccept 位置、失败回退）、行/块注释与 `@ignore` 空白跳过、关键字经 `keywordLookup`（ASCII 小写折叠）重分类、非法字符 → ERROR token + `stage="scan"` 诊断后继续、末尾补 EOF token；`@case-sensitive=false` 查表前折叠 ASCII 大写、lexeme 保留原文；`compiler_lexer` 追加两文件。temp Qt-free 冒烟 21 项 CHECK 全过（IF→if 折叠重分类且 lexeme 保留原文、`:=`/`>=` 最长匹配、块注释丢弃、ERROR 恢复、未闭合注释 Warning、空输入仅 EOF、未 configure 报错）。
- [x] **T1.8 生成 C++ 扫描器 + 编译运行**（2026-09-30，v2 方案已定稿并批准，e2e 通过，待人工 Ctrl+B 后提交）：
  - 新增 `src/lexer/SampleLexWriter.{h,cpp}`（**渲染单点化**：`renderHeader`/`writeToken`/`render` + `renderModeForKind`；头部省略 `#generated-at`）、`src/codegen/ScannerSourceGenerator.{h,cpp}`（方法二生成：内嵌 `ACC[]`/`KEYWORDS[]`/`IGN[256]`/`TOK_MODE[256]`/注释定界符/`COMMENT_CODES[]`/`START_STATE`，不嵌 256 转移表；`@@FOLD@@` 按 caseSensitive 注入折叠/恒等；每状态区间合并 + 关键字符分支；KEYWORDS 排序 + `std::lower_bound` 零动态分配）、`src/codegen/templates/scanner_main.cpp.in`。
  - `LexerCompilerRunner` 由 `src/core` 迁至 `src/gui` 并补全 QProcess（g++ 定位：显式配置 → `COMPILER_GXX` → 候选路径 → PATH）；`compiler_core` 去掉 runner 与 `Qt6::Core`，恢复零 Qt。
  - `src/app/main.cpp` 新增 `--lex <rules> <source> [output]` CLI 分支（不构造 QApplication，ifstream/ofstream 直读直写走完 return）；app 链接 compiler_lexer；codegen INTERFACE→STATIC + `COMPILER_CODEGEN_TEMPLATE_DIR` 编译定义；test/smoke 改链接 compiler_gui；ARCHITECTURE/FILE_FORMATS 同步。
  - **测试证据**：temp Qt-free e2e 3 组 × **18 CHECK 全过**（TINY 块注释/大小写不敏感/最长匹配、MiniC 关键字列表重分类/Comment DFA 丢弃/大小写敏感、非法字符 + 未闭合块注释）——宿主管线（Scanner + SampleLexWriter）与生成扫描器**逐字节一致**、无 `#generated-at`、头部格式正确；生成 `scanner.cpp` 独立 g++ `-O2 -Wall -Wextra` 编译 0 错 0 警、空目录独立运行，TINY 输出 `sample.lex` 与 FILE_FORMATS §4 完全一致（`IF`→1、`23 "x"`、`<=`→17、`:=`→22、无 EOF 行）；cmake configure 通过（codegen STATIC、编译定义带引号正确）；`LexerCompilerRunner.cpp`/`main.cpp`/`smoke_test.cpp` 独立 Qt 编译 0 错 0 警。**全量 Ctrl+B 受沙箱 AUTOMOC 限制未在本会话执行，待人工在 Qt Creator 复核**。
- `skeleton_smoke` 冒烟测试：因外部 CMD 缺少 Qt DLL 未独立运行；Qt Creator 内项目构建正常。
- [x] **T1.9 Task1Page GUI + 可视化 + LexerCompilerRunner 接线**（2026-10-01，本次提交）：
  - 新增 `src/pipeline/LexerPipeline.{h,cpp}`（零 Qt 流水线编排：Rules→NFA→DFA→Minimize→Codegen→Compile→Run；`CompileRunBackend` 抽象 + `PipelineSnapshot`；失效范围两级：规则变更全失效、源程序变更仅 Run 级失效；编译/运行失败回退进程内 `Scanner`+`SampleLexWriter` 预览并诚实标注，`runProducedLex` 区分真实运行）。
  - 新增 `src/gui/CompilerRunnerBackend.{h,cpp}`（包 `LexerCompilerRunner` 的 Qt 适配，T1.8 零修改）、`src/gui/Task1PipelineWorker.{h,cpp}`（独立 QThread + `requestStop()` 阶段边界取消 + 快照信号跨线程回传）、`src/gui/AutomatonTableView.{h,cpp}`（NFA/DFA 表格：转移区间合并、超阈值截断 + tooltip、NFA 边"N 条边"+tooltip）。
  - 改造 `src/gui/Task1Page.{h,cpp}`：顶部操作栏（打开 .rules/源程序、一键运行、5 个单步按钮）+ 6 个子 Tab + 单词编码 Tab 进程内回退诚实标注 badge；析构按 `requestStop → quit → wait → delete` 清理线程。
  - 接线 `MainWindow`（信号→LogDock/StatusBar，先建 Dock）、`LogDock::logDiagnostic`（统一 `[severity] [stage]` 格式）。
  - **深色主题修正**：`AutomatonTableView` 不填背景色，只用文字区分（起始列"→"、接受列"是"、编码列 tokenCode，标记列加粗），兼容任何主题。
  - 测试：`test/pipeline/lexer_pipeline_test.cpp` 零 Qt 单测 **43 CHECK 全过**（全流程 / 编译失败预览回退 / 运行失败回退 / 坏规则 / 失效范围 / 单步执行），`g++ -O2 -Wall -Wextra` 0 错 0 警；GUI 7 个源文件 `-fsyntax-only` 0 错；`compiler_pipeline` / `lexer_pipeline_test` CMake 构建通过。全量 Ctrl+B（AUTOMOC）由人工在 Qt Creator 复核，**GUI 全流程跑通、效果良好**。
- [x] **T1.10 TINY/Mini-C 语言包 + 端到端验收**（2026-10-07 完成，待人工 Qt Creator 复核后提交）：
  - 落地 `test/languages/tiny/{tiny.rules,tiny.bnf,samples/test.tiny}`、`test/languages/minic/{minic.rules,minic.bnf,samples/test.minic}`。目录按用户决定置于 `test/languages/<lang>/`（与 ARCHITECTURE.md §8 I5 根级 `languages/` 有偏差，T2「打开语言项目」入口需适配此路径）；`test/tiny.rules`、`test/sample.tny` 已 `git mv` 至新目录并升级为正式版（无代码引用旧路径，git 保留重命名历史）。
  - **TINY**：8 关键字（`KW_*_103~110 = "if"` 等引号字面量，display=拼写；关键字规则排在 ID 之前，TASKS T1.10 注意事项 3）、`ID_101`、`NUM_100`、16 个符号（`SYM_*_200~215`，沿用 T1.6 测试编码 200~212 并补齐 `%`=213、`^`=214、`<>`=215）、`@case-sensitive false`、`@comment block { }`；`{`/`}` 只作注释定界符不定义 token（TASKS §6.4）。样例覆盖全部单词与全部语句（含 `IF/THEN/END` 大写验证大小写不敏感）。
  - **Mini-C**：任务书 PDF（第二组测试数据）确认为 **9 个关键字**（`e1se` 笔误→`else`；`int float double void else if return while for`，编码 103~111，`int`=103 与任务书示例输出 `int a=b-1;` 一致）、`ID_101 = (_|letter)(_|letter|digit)*`、`NUM_100 = digit+(\.digit+)?`（含小数）、23 个符号（`SYM_*_200~223`，`-`=201、`=`=204 与任务书示例一致）、`@case-sensitive true`、`@comment line //`；单独 `_` 不单列（TASKS §6.5）。
  - **.bnf 仅落地不消费**（T2 才用）：TINY 依讲稿（FILE_FORMATS §3.3 同款）、Mini-C 依任务书 PDF 第 7-8 页 34 条产生式；已修正两处确认笔误（`e1se`→`else`、`statement` 中 `dowhile-stmt`→`while-stmt`）；空串用 `#`（FILE_FORMATS §3 约定；任务书 PDF 写 `@`，留待 T2 定稿）。
  - **验证证据**（2026-10-07）：`--lex` CLI 进程内扫描 + 生成扫描器（g++ 独立编译 0 错 0 警、运行退出码 0）两条路径 **TINY（26 token）与 Mini-C（35 token）逐字节一致**；TINY 最小 DFA 52→50、Mini-C 70→66 状态；sample.lex 头部/数据行逐项核对正确（关键字 display=拼写、`<>`/`%`/`^`/小数 NUM/`++`/`--`/`[]`、`//` 与 `{ }` 注释丢弃、非法字符恢复、无 EOF 行、无 `#generated-at`）。
  - 记录待办：TASKS.md T1.10 注意事项 #1「Mini-C 12 个关键字」为 T1.6 测试样例旧说法，与 PDF 实际 9 个不符，后续文档同步时修正。
- [x] **T2.1 `.bnf` 解析器 → Grammar**（2026-10-08 完成）：`src/grammar/BnfParser.{h,cpp}`（纯 core 依赖，零 Qt）——字符级切词（行首 `#` 注释 vs 产生式内 `#` 空串消歧、`ε`、`$`、引号字面量、多行续写、BOM）、两遍扫描判定非终结符、预置内部符号 id0=`$`/id1=`#`、ε 产生式 `rhs={}`、`@start` 缺失缺省取第一条左部 + Info；终结符→tokenCode 映射接口只认 `TokenCodeRegistry`（主 A：sample.lex 头部 `fromHeader`，T2.7 接线；备 B：重解析 `.rules`，T2.1 已用）；`src/grammar/CMakeLists.txt` INTERFACE→STATIC，顶层 `COMPILER_ENABLE_TASK2` 默认 ON。验收：`test/grammar/bnf_parser_test.cpp` **216 项 CHECK 全过**（tiny.bnf 17 非终结符/26 终结符/37 产生式与理论一致；minic.bnf 5 条 ε 产生式、全部终结符解析成功；负例诊断 stage="bnf-parse" + 行列号逐项符合），temp g++ 0 错 0 警 + CMake `bnf_parser_test` 目标构建运行通过 + Qt Creator Ctrl+B 全量通过。
- [x] **T2.7 SampleLexReader 读 sample.lex**（2026-10-08 完成）：`src/lexer/SampleLexReader.{h,cpp}`（与 SampleLexWriter 对称，零 Qt）——头部逐行到 `#token-table-end` + `TokenCodeRegistry::fromHeader` 重建 registry；数据区流式解析，字段形式与 writer **共用同一份 `renderModeForKind`**（mode 单点）；CodeOnly 词素回填 display、`\"`/`\\` 转义解码、未知编码 Error+恢复继续（失败时 value 仍携带部分数据）；EOF 不自动追加，`makeEofToken()` 供 T2.8；Token.loc.line = 数据区 token 序号（1 基，非源程序行号）。验收：`test/lexer/sample_lex_reader_test.cpp` **1184 项 CHECK 全过**——writer→reader 往返一致、tiny/minic 进程内扫描→渲染→读回一致（104/234 token，与 T1.10 真实产物 sample.lex 读回逐项吻合）、边界/负例 14 组（空文件、仅头部、数据区空行、截断、未登记编码、CodeOnly 多余字段、转义、BOM 等），temp g++ 0 错 0 警 + CMake `sample_lex_reader_test` ctest 通过 + Qt Creator Ctrl+B 全量通过。

## 历史记录 · 任务一实施过程

- **T1.6 .rules 解析 + TokenCodeRegistry 串联**（2026-09-21 启动，方案已定稿）：
  - 多规则合并采用**方案二（合并总 DFA）**：共起点 ε 拼接总 NFA → 一次子集构造 → 一次 Hopcroft 最小化；priority=文件顺序（0 基），复用 T1.3/1.4/1.5 既有机制。
  - 文件顺序：`LexerSpec.h` → `LexRulesParser.{h,cpp}` → `LexerBuilder.{h,cpp}`；`src/lexer/CMakeLists.txt` INTERFACE→STATIC；`TokenCodeRegistry` 新增显式编码重载（nextCode_ 单调递增 + 分配跳过已用编码）。
  - **已写入**（2026-09-21）：`src/lexer/LexerSpec.h`（含 `keywordLookup` 字段，折叠拼写→code）、`src/lexer/LexRulesParser.{h,cpp}`、`src/lexer/LexerBuilder.{h,cpp}`、`src/lexer/CMakeLists.txt` 改 STATIC（含全部 4 个文件）。**待人工复核后编译验证**。
  - **core 唯一改动**（2026-09-21，已单独编译验证）：`src/core/TokenCodeRegistry.h` 新增 `registerToken(const TokenType&, int explicitCode)` 显式编码重载（code∈[1,254]、名称/编码重复返回 -1、不推进 nextCode_）+ `allocateCode` 改为单调递增且跳过已用编码（耗尽返回 -1）+ `registerToken` 越界守卫；`g++ -std=c++17 -Wall -Wextra` 独立语法检查 0 错 0 警，临时语义测试 17 CHECK 全过（先显式 100 后自动得 1/2；已用 1、3 跳 2、再 4；0/255/重复均 -1）。
  - **T1.6 测试**（2026-09-21，temp 目录 Qt-free 编译，`g++ -O2 -Wall -Wextra` 0 错 0 警）：内嵌 TINY/Mini-C 两段 .rules 样例，LexRulesParser::parse → LexerBuilder::buildMinimalDfa 全链路，CHECK 全过。TINY：registry 编码正确（ID=101/Num=100/IF=103/.../SYM{=213），caseSensitive=false 折叠生效，最小 DFA 14 态/12 接受态；Mini-C（仅去重变体，去除重复 INT/=/-）：10 关键字 103–112、Comment=102 生成 DFA 且 kind=Comment、keywordLookup 10 项折叠键（'int'→103...），最小 DFA 26 态/23 接受态；原样 Mini-C 因重复 INT/= 触发『名称重复』诊断（符合预期，多错误收集）。**待人工复核**。
  - 5 项细节已确认：@ignore 默认已在文档；关键字 name=拼写本身；@name 可选；Comment 前缀判注释；registerToken 重载语义（先显式 100 后自动得 1/2；已用 1、3 时分配跳 2、再 4）。
  - **任务书核对结论**（`编译原理课程设计任务书.pdf`，2026-09-21）：输入格式 `digit=[0-9]`/`letter=[A-Za-z]` 为命名类定义行；`Name_<code>`/`Name<code>L` 显式编码；`L`=字面量列表（编码自该值递增，如 `Symbol_200L` 也生成 DFA）；`Keyword...L` 不生成 DFA 仅登记编码（INT=103/DOUBLE=104/BOOL=105）；`Comment_102` 生成 DFA 但输出丢弃；"多个生成 DFA 的正则之间实现或（选择）"印证方案二；空串用 `#`；`+`/`*` 作字面量需转义。
  - **sample.lex 数据行格式已定稿**（2026-09-30，任务书 PDF 为准）：空格分隔 `编码 [词素]`——关键字只输出编码、标识符 `编码 "lexeme"`、数字 `编码 数值`、符号只输出编码、注释不输出；不写 EOF 行、无行列号。FILE_FORMATS §4.3 已同步，不再待 T1.8 复核（详见下 T1.7）。

- **T1.7 表驱动扫描器**（2026-09-30 启动，方案已定稿）：
  - 文件：新增 `src/lexer/Scanner.h`、`src/lexer/Scanner.cpp`；`src/lexer/CMakeLists.txt` 追加两文件；**不动 core/regex/codegen/gui**。
  - 数据结构：`ScanResult`（`vector<Token>` + `registry` 拷贝 + `diagnostics`，stage="scan"）放 lexer 模块；`Scanner::configure(spec, minDfa)` 一次性预处理（`trans_[state][256]` 密集表 + `acceptCode_[state]` + `commentCodes_` 集），`Scanner::scan(text, path)` 可重复调用。
  - 扫描主循环（token 起点依次判定）：①ignoreChars 跳过 ②行注释 ③块注释 ④DFA 最长匹配（记录 lastAccept 位置、失败回退）⑤非法字符 → ERROR token + stage="scan" 诊断后继续；`Comment` 前缀规则的 token 丢弃；关键字经 `keywordLookup`（ASCII 小写折叠）重分类；末尾补 EOF token。
  - **@case-sensitive=false 修正**（关键）：T1.6 折叠的是规则 AST（DFA 表只存小写），但源程序原文未折叠——扫描查表时若 `spec.caseSensitive==false`，先把 `source[cur]` 的 ASCII 大写折叠为小写再查 `trans_`，**lexeme 保留原文**；否则扫 `IF` 会 `trans_[state]['I']==-1` 失败。注释定界符与 ignoreChars 精确匹配、不折叠。
  - 边界决策（已确认）：未闭合块注释 → `Warning` 继续；非 ASCII 字节 → ERROR token；空输入 → 仅 EOF；`kind` 必须正确（关键字重分类后为 Keyword）——T1.8 生成扫描器据 `kind` 决定 sample.lex 渲染（编码 only / 引号词素 / 数值）。
  - 验收：TINY 全单词样例输出正确 token 流（关键字重分类、最长匹配 `<=`/`:=`、注释丢弃、错误恢复、行列号正确）；temp 目录 Qt-free 编译 0 错 0 警。
  - **已写入**（2026-09-30）：`src/lexer/Scanner.h`、`src/lexer/Scanner.cpp`、`src/lexer/CMakeLists.txt` 追加；temp Qt-free 冒烟 21 项 CHECK 全过（IF→if 折叠重分类且 lexeme 保留原文、`:=`/`>=` 最长匹配、块注释丢弃、ERROR 恢复、未闭合注释 Warning、空输入仅 EOF、未 configure 报错）；`compiler_lexer` CMake 目标编译通过。**待人工 Ctrl+B 复核**。

- **T1.8 生成 C++ 扫描器 + 编译运行**（2026-09-30 启动，v2 方案已定稿并批准）：
  - 【A 生成风格】方法二：`state` 变量 + `while(!done){ switch(state){...} }`；每 case 内嵌**区间合并 + 关键字符分支**，多字符符号在后续 case 续读；循环外统一回溯 `lastCode>=0` 产 token / 否则 ERROR 单字符。
  - 【B 依赖与结构】零 Qt、仅 STL；内嵌 `ACC[]`/`KEYWORDS[]`/`IGN[256]`/`TOK_MODE[256]`/注释定界符/`COMMENT_CODES[]`/`START_STATE`，**不嵌 256 转移表**；`generated_lexer <input> [output]`；头部 `#file-format/#version/#language/#generator` + token 表 + `#source`，**省略 `#generated-at`**。
  - 【C LexerCompilerRunner】`src/core` → `src/gui` 迁移 + 补全 QProcess；g++ 定位：显式配置 → `COMPILER_GXX` 环境变量 → 候选路径（`C:\Qt\Tools\mingw1310_64\bin\g++.exe`、`E:\QTCreator\Tools\mingw1310_64\bin\g++.exe`）→ PATH g++。
  - 【D 关键陷阱】runner 迁移同步 ARCHITECTURE.md；core 去 `Qt6::Core`；生成代码零 Qt 不受 Qt DLL 影响；**渲染单点化**（宿主侧只有 SampleLexWriter 一份实现，codegen 复用它生成内嵌头部，保证逐字节一致）；`#generated-at` 省略。
  - 【E 文件】新增 `src/lexer/SampleLexWriter.{h,cpp}`、`src/codegen/ScannerSourceGenerator.{h,cpp}`、`src/codegen/templates/scanner_main.cpp.in`；runner 迁移 `src/gui`；core 去 runner+Qt6::Core；codegen INTERFACE→STATIC + 模板目录编译定义；app 链接 compiler_lexer + `--lex` 分支；test/smoke 连带改链接 compiler_gui；docs 同步。
  - 【F 测试】Qt-free 渲染单测 → 独立编译（空目录）→ e2e（样例源 → sample.lex）→ 逐字节比对（T1.7 Scanner + SampleLexWriter vs 生成扫描器）→ 格式验收 → --lex CLI 端到端 → Ctrl+B 全量构建。
  - 【实现细节 5 条】①foldByte：`@@FOLD@@` 占位符，caseSensitive=false 生成 ASCII 大写折叠、true 恒等；②KEYWORDS[] 静态排序数组 + `std::lower_bound`，零动态分配；③注释处理优先于 DFA；行注释消费到 `\n` 前；块注释未闭合 Warning 继续；④ERROR 恢复：`lastCode<0` 输出 `255 <单字符>`，begin++ 继续；⑤main.cpp `--lex` 分支不构造 QApplication，ifstream/ofstream 直读直写走完 return。
  - **已写入**（2026-09-30）：`SampleLexWriter.{h,cpp}`、`ScannerSourceGenerator.{h,cpp}`、`templates/scanner_main.cpp.in`、`src/gui/LexerCompilerRunner.{h,cpp}`（迁移补全）、`main.cpp --lex` 分支、CMake 与 docs 同步；temp Qt-free 逐字节比对 e2e 全过（证据见「已完成 · T1.8」）。**待人工 Ctrl+B 复核后提交**。

## 任务一收尾清单（历史）

- [x] T1.5 DFA 最小化（Hopcroft 算法）
- [x] T1.6 .rules 解析 + TokenCodeRegistry（已验收：Ctrl+B 编译通过、优先级机制 ALL CHECK 通过，commit 95e63e9 + ddfdb0c）
- [x] T1.7 表驱动扫描器（已提交 b0edf25）
- [x] T1.8 生成 C++ 扫描器 + 编译运行（v2 方案，逐字节比对 e2e 通过；已提交 4956fab）
- [x] T1.9 Task1Page GUI + 可视化 + LexerCompilerRunner 接线（T1.9 本次提交）
- [x] T1.10 TINY/Mini-C 语言包 + 端到端验收（2026-10-07 完成，待人工复核后提交）

## 注意事项

- 参考文档存在笔误与文法不一致（见 TASKS.md §6），实现前以任务书 PDF 为准。
- 生成扫描器必须零 Qt 依赖（codegen 输出不含 Qt）。
- **T1.8 已解决原已知偏差**：`LexerCompilerRunner` 已由 `src/core` 迁至 `src/gui`，`compiler_core` 恢复零 Qt（去掉 `Qt6::Core`）；ARCHITECTURE.md 已同步。
- sample.lex 头部**省略 `#generated-at`**（T1.8 v2 定稿），与 FILE_FORMATS §4.2 一致。
- sample.lex 渲染**单点化**：宿主侧只有 `SampleLexWriter` 一份实现；codegen 复用它生成内嵌头部与渲染模式，保证与生成扫描器逐字节一致（e2e 比对）。
- 每次写代码前先回看并更新本文件与相关设计文档。

## 发布环境强化（2026-11-04）：无 g++ 下的演示路径

**背景**：开发机在 Qt Creator 内运行，`LexerCompilerRunner` 的四级查找总能命中 MinGW 的 `g++`，编译必然成功；打包成独立文件夹交给评阅老师后，包内只有 Qt 运行库，四级查找全部落空，`compile` 阶段失败 → 只能走进程内预览（不落盘）。于是"任务一生成的 sample.lex"在评阅机上不存在，任务二/三无输入。

**三处改动**：

1. **输出目录解析**（`Task1Page::outputDir` / `Task3Page::outputDir`）：原实现固定 `applicationDirPath()/../generated`，发布包 exe 在根目录，产物实际写到**包外**的上一级。改为"同级 `generated/` 存在则用它（发布布局），否则退回上一级（开发布局），两者都不存在则用同级"。
2. **预置产物通道**（`JointPipeline::loadPresetLex` / `loadPresetLexFromOutputDir`）：显式载入 `generated/<语言名>/sample.lex`，词法阶段短路（不碰 Codegen/Compile/Run），`strictLexOk_` 置真，语法与 IR 照常推进。与"进程内预览"严格区分：预览永不落盘、永不进语法分析；预置产物是外部真实运行结果。目录名与 `@name` 大小写不一致时退化为扫描 `outputDir/*/sample.lex`。
3. **失效绑定**（`JointPipeline::setSourceText` / `setRulesText` / `setOutputDir`）：预置产物与"当时的输入"绑定，源程序/规则/输出目录一变即作废并退回 `None`，避免旧 token 流装新程序。为此给 `LexerPipeline` 增公开只读 `sourceText()`（已去 BOM），比较以它为准。
4. **GUI**：任务三工具栏新增「载入预置 sample.lex」（`JointRunMode::Preset`），结果角标显示"◎ 预置产物"，与"✓ 真实生成扫描器产出""✗ 进程内预览"并列。

**配置修正**（顺带发现，影响全新克隆）：根 `CMakeLists.txt` 的 `option(COMPILER_ENABLE_TASK2 ...)` 原在 `add_subdirectory(src/pipeline)` 之后声明。全新配置（缓存为空）时 pipeline 读不到该变量，会静默跳过 `ParserPipeline/JointPipeline`，直到编译 GUI 才报 `FirstFollow.h: No such file or directory`；二次 configure 因缓存已有 ON 而"自愈"。已把 `option()` 提到 `add_subdirectory` 之前。

**测试**：`joint_pipeline_test` 新增 ⑧ 用例（16 项断言，`failCompile = true` 模拟无 g++）：断言自动定位、`presetLex` 标记、`compileCalls == 0`、语法树非空、IR 139 条、源程序变更后预置作废回退 `None`。全量 `ctest` 13/13 通过（需把 Qt `bin` 加入 PATH，否则 `skeleton_smoke` 报 0xc0000135）。Release 版 exe 已从零重建并在发布目录内 `--parse` / `--lex` 冒烟通过。

**文档**：REPORT.md 新增 2.4.6「跨环境编译器缺失与容错降级策略」（含表 2-7 三环境对照）；5.4 增第四条结论与不足其七（运行期对 `g++` 是软依赖）。发布包新增 `README.txt`（UTF-8 BOM，按实际 `test/languages/` 结构描述）。
