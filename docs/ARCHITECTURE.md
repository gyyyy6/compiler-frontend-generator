# 编译原理课程设计 · 总体架构设计（ARCHITECTURE）

> 版本：v0.9（T3.5 落地任务三 GUI 4 Tab + Task3PipelineWorker + `--minic` CLI）
> 日期：2026-11-02
> 状态：任务一/二已实现；任务三 T3.1~T3.5 实现完成（T3.5 待评审/提交）
> 配套文档：[DATA_STRUCTURES.md](./DATA_STRUCTURES.md) · [FILE_FORMATS.md](./FILE_FORMATS.md) · [TASKS.md](./TASKS.md) · [PROGRESS.md](./PROGRESS.md)

## 1. 全局任务总览

| 任务 | 内容 | 交付物 |
| ---- | ---- | ---- |
| 任务一 | 正则表达式 → NFA → DFA → 最小化 DFA → 生成 C/C++ 词法分析源程序 → 编译测试 → 输出单词编码文件 `sample.lex` | 可独立编译运行的词法分析器 + `sample.lex` |
| 任务二 | BNF 文法 → 文法化简 → First/Follow → LR(0) DFA → LALR(1) DFA → LALR(1) 分析表 → 读取 `sample.lex` 语法分析 → 生成语法树 →（可选）中间代码 | 语法分析器 + 语法树 |
| 任务三 | 用 Mini-C 语言对任务一/二做端到端联合测试：真实生成扫描器产出 `sample.lex` → LALR(1) 语法树 → 四元式 IR | 联合测试界面 + 测试报告 + `sample.lex`/语法树/IR 证据 |

三个任务集成在**同一个 Qt Widgets 应用**中，主窗口用 Tab 区分（硬性要求 1、2、7）。

## 2. 设计原则

- **P1 单一应用**：任务一、二、三共用主窗口与后端服务，Tab 隔离页面。
- **P2 语言无关**：所有语言相关行为由配置文件（`.rules` / `.bnf`）驱动，禁止把只适用于 TINY 的逻辑写死在代码中（硬性要求 3）。
- **P3 统一数据结构**：Token / Symbol / Production / Grammar / NFA / DFA / ParseTreeNode 在 `core` 模块统一定义，全工程共用（硬性要求 4）。
- **P4 统一文件格式**：词法规则文件 `.rules`、文法文件 `.bnf`、单词编码文件 `sample.lex` 三套统一格式（硬性要求 5）。
- **P5 core 不依赖 Qt**：核心数据结构与算法只用 C++17 + STL（`LexerCompilerRunner` 自 T1.8 起位于 `src/gui`，core 不再链接 Qt）。生成的语言分析器因此可以脱离本工程独立编译运行（硬性要求 6）。
- **P6 接口先行**：任务一完成后，任务二/三所需的数据结构与文件契约已全部就绪；任务二/三仅实现算法，不重做接口（硬性要求 9）。
- **P7 文档先行**：每次实现前先更新 `docs/`，再写代码（硬性要求 8）。

## 3. 总体架构图

```text
┌───────────────────────────────────────────────────────────────┐
│                     app · Qt Widgets UI                       │
│  MainWindow (QMainWindow)                                     │
│    菜单：文件 / 任务 / 工具 / 视图 / 帮助                       │
│    centralWidget: QTabWidget                                  │
│      ├─ Tab1 任务一 · 词法分析    (Task1Page)                  │
│      ├─ Tab2 任务二 · 语法分析    (Task2Page)                  │
│      └─ Tab3 任务三 · Mini-C 联合测试 (Task3Page)              │
│    Dock：日志面板 LogDock   StatusBar：语言项目/步骤/结果       │
└──────────────────────────┬────────────────────────────────────┘
                           │ Qt 信号/槽 + 服务调用
┌──────────────────────────▼────────────────────────────────────┐
│        pipeline / 服务编排（controller，零 Qt 核心 + gui 适配）  │
│  LexerPipeline (src/pipeline, T1)                             │
│  ParserPipeline (src/pipeline, T2) · JointPipeline (T3)       │
│  CompileRunBackend (抽象) · CompilerRunnerBackend (src/gui)   │
└──────────────────────────┬────────────────────────────────────┘
                           │ 直接调用
┌──────────────────────────▼────────────────────────────────────┐
│           core · 统一数据结构（纯 C++17，无 Qt 依赖）           │
│  Token · Symbol · Production · Grammar · NFA · DFA             │
│  ParseTreeNode · Diagnostic · Result · TokenCodeRegistry       │
└───┬─────────┬─────────┬─────────┬─────────┬────────────────────┘
    │         │         │         │         │
┌───▼───┐ ┌───▼────┐ ┌──▼──────┐ ┌──▼──────┐ ┌──▼───────────────┐
│ regex │ │ lexer  │ │ grammar │ │ parser  │ │ codegen           │
│ RE→NFA│ │ 规则解析│ │ (T2.1~3)│ │ (T2.4~8)│ │ C/C++ 扫描器生成   │
│ 子集  │ │ DFA 生成│ │ 分析表  │ │ LR 驱动 │ │ 编译 & 运行        │
│ Hopcroft││ 表驱动 │ │        │ │ 语法树  │ │                   │
└───────┘ └────────┘ └────────┘ └────────┘ └───────────────────┘
```

## 4. 全局模块划分

| 模块 | 目录 | 职责 | 依赖 | 归属任务 |
| ---- | ---- | ---- | ---- | ---- |
| `core` | `core/` | 统一数据结构、诊断、TokenCodeRegistry、Result | 仅 STL | T1/T2/T3 |
| `regex` | `regex/` | 正则表达式解析（AST）、Thompson 构造 RE→NFA、子集构造 NFA→DFA、Hopcroft 最小化、字符集运算 | core | T1（T2 复用字符集工具） |
| `lexer` | `lexer/` | `.rules` 解析、LexerBuilder（规则→DFA）、通用表驱动扫描器、`sample.lex` 读写 | core, regex | T1/T3 |
| `grammar` | `grammar/` | BNF 解析（T2.1 已启动：`BnfParser`）、文法化简、First/Follow、LR(0)、LALR(1)、分析表 | core | T2 |
| `parser` | `parser/` | LR 自动机 + 分析表 + LR 驱动（T2.4~T2.8 已实现：`Lr0Automaton`/`Lalr1Builder`/`ParseTable`/`LrDriver`）；错误处理按决策点 5 B 遇错即停，无 panic-mode 恢复 | core, grammar | T2/T3 |
| `ir` | `ir/` | 中间代码生成（T2.9，任务书选做转必做）：ParseTree → 四元式 IR（`IrGenerator`），op 白名单/临时变量/标签/回填；T3.2 补齐数组下标左值写回（`[]=`） | parser, grammar, core | T2/T3 |
| `codegen` | `codegen/` | 由最小 DFA 生成独立 C++ 扫描器源码（方法二，零 Qt） | core, lexer | T1 |
| `pipeline` | `pipeline/` | 任务一/二/三流水线编排（零 Qt）：`LexerPipeline`（T1.9：Rules→NFA→DFA→最小DFA→Codegen→Compile→Run + `CompileRunBackend` 抽象）；`ParserPipeline`（T2.10：LexRead→BnfParse→化简→First/Follow→LR(0)→LALR(1)→分析表→LR驱动→IR，**LexRead 前置**）；`JointPipeline`（T3.4：严格串联 LexerPipeline→ParserPipeline，强制 `runProducedLex == true`，拒绝进程内预览回退） | core, regex, lexer, codegen（+T2：grammar, parser, ir） | T1/T2/T3 |
| `app` | `app/` | Qt 主窗口、三个任务页、日志与可视化、语言项目管理 | 以上全部 | T1/T2/T3 |
| `gui` | `gui/` | Qt 页面（MainWindow/LogDock/Task*Page）+ `LexerCompilerRunner`（T1.8，QProcess）+ `CompilerRunnerBackend`/`Task1PipelineWorker`/`AutomatonTableView`（T1.9）+ `Task2PipelineWorker`/`LrTableView`/`ParseTreeView`（T2.10）+ `Task3PipelineWorker`（T3.5，联合测试线程） | core, lexer, codegen, pipeline（Qt6） | T1/T2/T3 |

依赖规则：`core` ← `regex` ← `lexer`；`codegen` 依赖 `core`/`lexer`；`app` 只依赖各模块公开接口。**`grammar`/`parser` 不得被任务一反向依赖**。

## 5. 关键设计决策

- **D1 单词编码**：词法规则文件按行声明规则，`TokenCodeRegistry` 依文件顺序分配编码（1..N）；`0=EOF`、`255=ERROR` 为保留编码。`sample.lex` 头部自带 `code→name→display` 表，自描述，任务二无需外部映射。
- **D2 词法匹配策略**：最长匹配优先；长度相同按规则文件顺序（先声明者优先），用于解决 `<=` 与 `<`、关键字与 `ID` 的冲突。
- **D3 注释处理**：注释定界符在扫描器内处理（块注释、行注释由 `.rules` 声明），注释与空白一样被跳过，**不输出**为 token。TINY 的 `{...}` 与 Mini-C 的 `//` 均由此配置驱动。
- **D4 大小写**：`@case-sensitive false` 时，字面量字符与字符类自动扩展为大小写两套字符（用于 TINY 关键字/标识符不区分大小写）；Mini-C 为 `true`。
- **D5 扫描器形态**：运行时扫描器与生成代码共用同一份匹配算法（最长匹配 + 回溯）；生成代码采用方法二（`state` 变量 + `while(!done){ switch(state){...} }`），把最小 DFA 内嵌为常量数组（`ACC[]`/`START_STATE`）+ 每状态区间合并分支，**不嵌 256 转移表**，保证**零外部依赖、可独立编译**。
- **D6 目录即语言项目**：`test/languages/<lang>/` 下同时放 `.rules` 与 `.bnf`（以及 `samples/`），主窗口以"打开语言目录"为统一入口，任务二/三沿用。
- **D7 错误处理**：所有阶段（正则解析/子集构造/编译运行）统一产生 `Diagnostic`，带阶段名、行列号与严重级别；UI 层统一汇聚到日志面板。

## 6. 项目目录结构

```text
编译原理课程设计/
├── CMakeLists.txt
├── docs/                            # 设计文档（先文档后代码）
│   ├── ARCHITECTURE.md
│   ├── DATA_STRUCTURES.md
│   ├── FILE_FORMATS.md
│   ├── TASKS.md
│   └── PROGRESS.md
├── src/
│   ├── core/                        # 统一数据结构（纯 C++17，零 Qt，T1.2）
│   │   ├── CMakeLists.txt
│   │   ├── Token.h / TokenCodeRegistry.h / Symbol.h / Grammar.h
│   │   └── NFA.h / DFA.h / ParseTree.h / Diagnostic.h / Result.h
│   ├── regex/                       # 正则引擎（T1.3~T1.5）
│   │   ├── CMakeLists.txt
│   │   ├── RegexAst.h / RegexParser.{h,cpp} / RegexToNfa.{h,cpp}
│   │   └── NfaToDfa.{h,cpp} / MinimizeDfa.{h,cpp}
│   ├── lexer/                       # 词法：规则解析/装配/扫描/渲染（T1.6~T1.8）
│   │   ├── CMakeLists.txt
│   │   ├── LexerSpec.h / LexRulesParser.{h,cpp} / LexerBuilder.{h,cpp}
│   │   └── Scanner.{h,cpp} / SampleLexWriter.{h,cpp}
│   ├── codegen/                     # 最小 DFA → 独立 C++ 扫描器源码（T1.8）
│   │   ├── CMakeLists.txt
│   │   ├── ScannerSourceGenerator.{h,cpp}
│   │   └── templates/scanner_main.cpp.in
│   ├── pipeline/                    # 任务一/二/三流水线编排（零 Qt）
│   │   ├── CMakeLists.txt
│   │   ├── LexerPipeline.{h,cpp}    # T1.9：词法分析流水线
│   │   ├── ParserPipeline.{h,cpp}   # T2.10：语法分析流水线（LexRead 前置）
│   │   └── JointPipeline.{h,cpp}    # T3.4：LexerPipeline→ParserPipeline 严格联合
│   ├── gui/                         # Qt 页面 + 编译运行适配（T1.1/T1.8/T1.9/T2.10）
│   │   ├── CMakeLists.txt
│   │   ├── MainWindow.{h,cpp} / LogDock.{h,cpp}
│   │   ├── Task1Page.{h,cpp} / Task2Page.{h,cpp} / Task3Page.{h,cpp}
│   │   ├── LexerCompilerRunner.{h,cpp}            # T1.8：QProcess 编译/运行
│   │   ├── CompilerRunnerBackend.{h,cpp}          # T1.9：pipeline 后端适配
│   │   ├── Task1PipelineWorker.{h,cpp}            # T1.9：任务一工作线程
│   │   ├── AutomatonTableView.{h,cpp}             # T1.9：NFA/DFA 表格
│   │   ├── Task2PipelineWorker.{h,cpp}            # T2.10：任务二工作线程
│   │   ├── LrTableView.{h,cpp}                    # T2.10：LR(0)/LALR(1)/分析表表格
│   │   ├── ParseTreeView.{h,cpp}                  # T2.10：语法树 QTreeWidget
│   │   └── Task3PipelineWorker.{h,cpp}            # T3.5：任务三联合测试线程
│   ├── app/
│   │   ├── CMakeLists.txt
│   │   └── main.cpp                 # 含 --lex（T1.8）/ --parse（T2.10）/ --minic（T3.5）CLI 分支
│   ├── grammar/                     # 任务二：BNF→Grammar（T2.1 起，COMPILER_ENABLE_TASK2 默认 ON）
│   │   ├── CMakeLists.txt
│   │   └── BnfParser.{h,cpp}        # T2.1：.bnf 文本 → Result<Grammar>（终结符→tokenCode 映射）
│   ├── parser/                      # 任务二：LR 自动机 + 分析表 + LR 驱动（T2.4~T2.8 已落地）
│   │   ├── CMakeLists.txt
│   │   ├── Lr0Automaton.{h,cpp}     # T2.4：LR(0) 项目集族
│   │   ├── Lalr1Builder.{h,cpp}     # T2.5：LALR(1)（LR(1) + 同心合并）
│   │   ├── ParseTable.{h,cpp}       # T2.6：LALR(1) 分析表 ACTION/GOTO + 冲突报告
│   │   └── LrDriver.{h,cpp}         # T2.8：LR 驱动 + ParseTree 构建（进程内）
│   ├── ir/                          # 任务二：中间代码生成（T2.9，选做转必做）
│   │   ├── CMakeLists.txt
│   │   └── IrGenerator.{h,cpp}      # T2.9：ParseTree → 四元式 IR（回填式标签）
├── test/
│   ├── CMakeLists.txt
│   ├── smoke/smoke_test.cpp         # 骨架冒烟
│   ├── languages/                   # 语言包（T1.10 落地：tiny / minic）
│   │   ├── tiny/{tiny.rules, tiny.bnf, samples/}
│   │   └── minic/{minic.rules, minic.bnf, samples/}
│   ├── pipeline/lexer_pipeline_test.cpp   # T1.9 零 Qt 单测
│   ├── pipeline/parser_pipeline_test.cpp  # T2.10 零 Qt 单测
│   └── pipeline/joint_pipeline_test.cpp   # T3.4 零 Qt 联合编排单测
└── build/                           # 构建产物（不提交）
```

## 7. 主窗口与任务三页面方案

主窗口为 `QMainWindow`，中央 `QTabWidget`，日志为可关闭 `QDockWidget`。

```text
┌──────────────────────────────────────────────────────────────┐
│ 菜单: 文件 | 任务 | 工具 | 视图 | 帮助                         │
├──────────────────────────────────────────────────────────────┤
│ Tab1 任务一·词法分析 │ Tab2 任务二·语法分析 │ Tab3 任务三·联合测试 │
│ ┌──────────────────────────────────────────────────────────┐ │
│ │ Task3Page：语言包/源程序/一键联合测试                     │ │
│ │  Tab1 源程序+覆盖检查 | Tab2 词法结果                     │ │
│ │  Tab3 语法结果       | Tab4 联合测试报告                  │ │
│ └──────────────────────────────────────────────────────────┘ │
├──────────────────────────────────────────────────────────────┤
│ Dock: 日志（诊断信息统一汇聚）                                 │
│ StatusBar: 当前语言项目 · 当前阶段 · 最近结果                   │
└──────────────────────────────────────────────────────────────┘
```

- **菜单**：
  - 文件：新建/打开语言项目（`test/languages/<lang>/`）；打开/保存 规则文件、源程序、BNF 文法、`sample.lex`。
  - 任务：任务一 / 任务二 / 任务三（切换到对应 Tab，快捷键 Ctrl+1/2/3）。
  - 工具：编译器设置（g++/clang++/cl 路径、参数、是否保留生成源码）、一键执行任务一、清空输出。
  - 视图：日志面板开关、重置布局。
  - 帮助：关于。
- **Task1Page**（T1.9 已实现）：
  - 顶部操作栏：打开 .rules / 打开源程序 / 一键运行 / 只生成 NFA / 只生成 DFA / 只最小化 / 只生成代码 / 只编译测试；下方显示规则/源程序路径与状态。
  - 6 个子 Tab：正则表达式（`QPlainTextEdit` 可编辑）/ NFA / DFA / 最小DFA（`QTableWidget` 状态转换表，起始/接受高亮、大字母表区间合并 + 截断 + tooltip）/ 词法源程序（`generated_lexer.cpp` 只读）/ 单词编码（`sample.lex` 只读，进程内回退时标注"[进程内预览，非真实运行结果]"）。
  - 流水线（`src/pipeline/LexerPipeline`）在独立线程（`Task1PipelineWorker`）中执行；任一步失败即在日志 Dock 显示 stage/message；产物输出到 `build/generated/<语言名>/`。
- **Task2Page**（T2.10 已实现）：
  - 顶部操作栏：打开 .bnf / 打开 sample.lex / 一键运行 / 只解析 / 只化简 / 只 First-Follow / 只 LR(0) / 只 LALR / 只分析表 / 只解析语法树 / 只生成 IR；下方显示 BNF/sample.lex 路径与状态。
  - 6 个子 Tab：BNF 文法（`QPlainTextEdit` 可编辑）/ 化简后文法（`QPlainTextEdit`：删除日志 + 带 id 产生式）/ First·Follow（`QTableWidget`，每非终结符一行）/ LR(0)+LALR(1) 项目集（内嵌子 Tab，`QTableWidget`：状态/起始/接受/项目集/转移）/ 分析表 ACTION/GOTO（`QTableWidget` 分组表头，冲突单元格高亮 + 底部冲突清单）/ 语法树（`QTreeWidget`，内部节点带 productionId；下半区显示四元式 IR）。
  - 流水线（`src/pipeline/ParserPipeline`）在独立线程（`Task2PipelineWorker`）执行；**LexRead 前置**（BnfParser 需 registry 做终结符映射）；无 sample.lex 时 ≤ 分析表降级（tokenCode=-1 + Info），语法树/IR 需 sample.lex；任一步失败在日志 Dock 显示 stage/message。
- **Task3Page**（T3.5 已实现）：
  - **定位**：任务三联合测试控制台，不嵌入、不直接复用 `Task1Page`/`Task2Page` 页面；复用其 `LexerPipeline`、`ParserPipeline`、`CompilerRunnerBackend`、`populateParseTree()`、`formatIrProgram()` 等底层能力。
  - **顶部操作栏**：打开 Mini-C 语言包 / 打开源程序 / 保存源程序 / 一键联合测试 / 仅词法测试 / 仅语法测试 / 导出测试报告；显示语言包、源程序、状态与耗时。
  - **Tab1 测试源程序 + 覆盖检查**：Mini-C 源码编辑；关键字/符号/文法分支覆盖矩阵；未覆盖项高亮。
  - **Tab2 词法结果**：Token 表（code/name/kind/lexeme/顺序）、`sample.lex` 只读预览、token 统计、ERROR 数、生成扫描器路径。`sample.lex` 不含源程序行列号，故「顺序」为数据区序号（不伪造行列）。
  - **Tab3 语法结果**：复用 `populateParseTree()` 显示语法树；显示 LR(0)/LALR(1) 状态数、冲突清单（悬挂 else）；复用 `formatIrProgram()` 显示四元式。
  - **Tab4 联合测试报告**：阶段时间线（Rules→NFA→DFA→Minimize→Codegen→Compile→Run→LexRead→BnfParse→Simplify→FirstFollow→LR0→LALR1→ParseTable→Drive→IR）、诊断汇总、覆盖矩阵、Markdown 导出。报告/覆盖渲染由 `Task3PipelineWorker` 的自由函数统一提供，GUI 与 `--minic` 共用同一份实现。
  - **严格性**：`JointPipeline` 必须校验 `runProducedLex == true`；若任务一编译/运行失败，只显示预览并标红，不进入语法分析。IR 作为独立阶段状态显示。
  - **跳转**：提供"查看任务一词法中间产物"、"查看任务二完整分析表"两个按钮，分别切到 Tab1/Tab2，不复制其内部状态。
- **页面组织**：当前代码未引入 `TaskPageBase`；Task1/2/3 统一采用“Page + Worker(QThread) + Pipeline(零 Qt)”模式。任务三不为此引入大重构。

## 8. 任务一为任务二/三提供的接口

| 编号 | 预留内容 | 说明 | 服务对象 |
| ---- | ---- | ---- | ---- |
| I1 | `Token` + `TokenCodeRegistry` | 任务一即实现并填充；**T2.1 已启用**：终结符→tokenCode 映射（主 A 读 `sample.lex` 头部 `fromHeader` 重建；备 B 重解析 `.rules`）；任务三直接复用内存中的 token 流 | T2/T3 |
| I2 | `sample.lex` 自描述格式 | 头部含 `code↔name↔display` 表，`SampleLexReader` 可直接消费，无需外部配置 | T2 |
| I3 | `Grammar`/`Symbol`/`Production`/`ParseTreeNode` | 在 core 中先行定义（纯数据结构，无算法），任务二 BNF 解析与语法树构建直接使用 | T2/T3 |
| I4 | `LexerPipeline`/`ParserPipeline` + `JointPipeline` | `JointPipeline`（T3.4）严格串联任务一/二：先生成扫描器运行产出 `sample.lex`，再以其文本驱动语法分析；任务三"一键联合测试"即调它 | T3 |
| I5 | 语言项目目录约定 | `test/languages/<lang>/{<lang>.rules,<lang>.bnf,samples/}`，主窗口统一入口"打开语言项目" | T2/T3 |
| I6 | `grammar/`、`parser/`、`ir/` 目录 | **T2.1 起实现算法**：`grammar/` 已落地 `BnfParser.{h,cpp}`（.bnf → Grammar）、`GrammarSimplifier`（T2.2 化简）、`FirstFollow`（T2.3 First/Follow/Nullable）；`parser/` 自 T2.4 起由 INTERFACE 占位转 STATIC，落地 `Lr0Automaton.{h,cpp}`（T2.4 LR(0) 项目集族）、`Lalr1Builder.{h,cpp}`（T2.5 LALR(1)：LR(1) + 同心合并）、`ParseTable.{h,cpp}`（T2.6 分析表 ACTION/GOTO + 冲突报告，决策点 6：冲突一律报告）与 `LrDriver.{h,cpp}`（T2.8 LR 驱动 + ParseTree 构建，决策点 4 进程内 / 决策点 5 B 遇错即停）；`ir/` 新增 `IrGenerator.{h,cpp}`（T2.9 中间代码生成，ParseTree → 四元式，选做转必做）；依赖方向 `app → ir → parser → grammar → core` | T2/T3 |
| I7 | CLI 入口 | 程序支持 `--lex <rules> <source> [output]`（T1.8）、`--parse <bnf> <sample.lex> [output-ir]`（T2.10）、`--minic <langDir> <source> [outputDir]`（T3.5：联合流水线，输出 `sample.lex`/tree/IR/report，退出码反映必需阶段） | T2/T3 |
| I8 | `CompileRunBackend` 抽象 + `LexerCompilerRunner`（`src/gui`） | 任务一经 `CompilerRunnerBackend` 编译生成的词法分析器；任务三联合流水线复用同一后端 | T2/T3 |
| I9 | `Task3PipelineWorker`（`src/gui`） | 在独立 QThread 中执行 `JointPipeline`，回传 `JointSnapshot`；页面关闭时按 T1/T2 模式停止/等待线程 | T3 |

## 9. 三任务数据流（贯通）

```text
.rules ─► 正则解析 ─► NFA ─► DFA ─► 最小 DFA ─► 生成 C++ 扫描器 ─► 编译
                                                                  │
source ───────────────────────────────────────────────────────────▼
                                                          运行生成扫描器
                                                                  │
                                                                  ▼
                                                        sample.lex（自描述）
                                                                  │
                                          头部 registry（LexRead 前置）
                                                                  ▼
.bnf ─► BNF解析 ─► 文法化简 ─► First/Follow ─► LR(0) ─► LALR(1) ─► 分析表
        │ BnfParser 以 sample.lex 头部 registry 做终结符→tokenCode 映射  │
        │                                                        数据行 token 流
        └───────────────────────────────────────────────────────────▼
                                                          LR 驱动 + 语法树构建
                                                                  │
                                                                  ▼
                                                         语法树 ─► 四元式 IR
```

任务三 `JointPipeline` 的严格路径：

```text
LexerPipeline::runThrough(Run)
  -> 必须 runProducedLex == true（真实生成扫描器产出的 sample.lex）
  -> 取 snapshot.runLexText
ParserPipeline::setSampleLexText(runLexText)
ParserPipeline::runThrough(Drive)      // 任务三必做：语法树
ParserPipeline::runThrough(Ir)         // 项目必做扩展：四元式（T3.2 后支持数组左值）
```

约束：

- 任务三**不得**用进程内预览替代生成扫描器产物；预览只用于失败诊断和界面提示。
- 任务三必做结果是词法 token 流 + 语法树；IR 作为项目选做转必做的扩展阶段单独显示。
- `sample.lex`、语法树文本、IR 文本和测试报告需从 `build/generated/<lang>/` 复制到 `docs/test-reports/artifacts/` 以便提交。

### 9.1 JointPipeline 接口与阶段映射（T3.4 定稿）

`src/pipeline/JointPipeline.{h,cpp}`（零 Qt，仅依赖 `LexerPipeline` + `ParserPipeline`）：

```text
JointStage::None ─► LexRun ─► Parse ─► Ir
                   │          │        │
                   │          │        └ LexerPipeline 无、ParserPipeline::runThrough(Ir)
                   │          └ ParserPipeline::runThrough(Drive)（LexRead..Drive）
                   └ LexerPipeline::runThrough(Run)（Rules..Run，强制真实产出）
```

| 联合阶段 | 内部推进（一次一阶段） | 成功判据 |
| ---- | ---- | ---- |
| `LexRun` | `Rules → Nfa → Dfa → Minimize → Codegen → Compile → Run` | `runThrough` 返回 true **且** `snapshot().runProducedLex == true` |
| `Parse` | `LexRead → BnfParse → Simplify → FirstFollow → Lr0 → Lalr1 → ParseTable → Drive` | 语法树非空、根为化简后开始符号 |
| `Ir` | `Ir`（`IrGenerator`） | 四元式序列非空 |

- **强制校验（决策点 4，绝对）**：`LexRun` 结束后必须复核 `runProducedLex`。为 `false` 时立即终止并区分两种诊断——已到 `Compile`/`Run`（或存在预览文本）记 `stage="joint-strict"`（预览回退，绝对拒绝）；上游阶段就失败记 `stage="joint-lex"`（无 sample.lex 可产）。`Parse`/`Ir` 前置 `strictLexOk_` 检查，未通过时语法流水线**一次也不调用**（诊断 `stage="joint-parse"`）。预览文本仅用于诊断，绝不进入语法分析。
- **`JointSnapshot`（值语义，跨线程）**：`lex`（`PipelineSnapshot`：NFA/DFA/最小DFA/生成源码/`runLexText`）、`parse`（`ParserPipelineSnapshot`：Grammar/First-Follow/LR(0)/LALR(1)/分析表/token 流/语法树/IR）、`sampleLexPath`、`sourcePath`、`runProducedLex`、`strictLexOk`、`lexStage`/`parseStage`/`validThrough`、`languageName`、合并诊断（词法在前、语法在后）。
- **推进粒度**：`runThrough(JointStage)` 供一键联合测试；`runLexerThrough(Stage)`、`runParserThrough(ParseStage)` 供 GUI 单步；`setStageCallback`/`setLexStageCallback`/`setParseStageCallback` 供进度显示；`setStopCallback` 转发给两条子流水线，保证关窗可中止。
- **失效规则**：`setRulesText`/`setSourceText`/`setSourcePath`/`setOutputDir` → `LexRun` 起（含 `Parse`/`Ir`）失效并复位 `strictLexOk_`，同时调用 `ParserPipeline::reset()`（T3.4 新增的公开复位入口）清空语法侧全部阶段与诊断，避免旧语法树被当成新结果；`setBnfText` → 仅 `Parse`/`Ir` 失效（`LexRead` 保留）。
- **诊断合并**：子流水线每次 `runThrough` 都会清空自身诊断，故联合层按阶段**累积**词法/语法诊断，并追加联合层结论，顺序固定为「词法 → 语法 → 联合」。
- **输入入口**：`loadInputs(rulesPath, bnfPath, sourcePath)` 一次性读取三个文件（UTF-8 去 BOM），供 T3.5 的 `--minic` CLI 与 `Task3Page` 复用。

## 10. 构建与运行

- 环境：CMake ≥ 3.16；Qt6 Widgets（兼容 Qt5）；编译器 g++/clang++/MSVC（C++17）。
- 顶层 `CMakeLists.txt`：`add_subdirectory(core)`、`add_subdirectory(regex)`、`add_subdirectory(lexer)`、`add_subdirectory(codegen)`、`add_subdirectory(pipeline)`、`add_subdirectory(gui)`、`add_subdirectory(app)`；`grammar`/`parser`/`ir` 由 `COMPILER_ENABLE_TASK2` 开关控制，**T2.1 起默认 ON**。
- 构建：

  ```bash
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j
  ./build/app/CompilerCourseApp.exe
  ```

- 生成词法分析器的独立编译：`codegen`（`ScannerSourceGenerator`）把最小 DFA + 模板渲染为独立 C++ 源码；`pipeline`（`LexerPipeline`，T1.9）编排 解析→NFA→DFA→最小化→生成→编译→运行，产物输出 `build/generated/<lang>/generated_lexer.cpp` 与 `<lang>_scanner(.exe)`（模板 `src/codegen/templates/scanner_main.cpp.in`）；编译/运行经 `CompileRunBackend` 抽象由 `gui` 的 `CompilerRunnerBackend`（包 `LexerCompilerRunner`，QProcess）执行，运行格式：`<lang>_scanner <源文件> [输出文件]`，缺省输出 `sample.lex`。
- 任务三联合测试（T3.5）：

  ```bash
  ./build/app/CompilerCourseApp.exe --minic test/languages/minic \
      test/languages/minic/samples/test.minic        # 第三参数可省略，缺省 build/generated
  ```

  产物：`build/generated/MiniC/sample.lex`、`test.minic.tree.txt`、`test.minic.ir.txt`、`test.minic.report.md`；退出码 0 表示词法（真实生成扫描器）+ 语法树均成功，3 表示必需阶段失败（仍写报告），5 表示 IR 失败。
- GUI 任务三：加载 `test/languages/minic`，打开 `samples/test.minic`，点击“一键联合测试”；四个 Tab 分别展示源程序/覆盖、词法结果、语法结果、联合报告。

## 11. 风险与注意

- 参考文档存在笔误/不一致（详见 TASKS.md 第 6 节），任务三实现前须对照任务书 PDF 复核。
- `test/languages/` 下的 `.rules` 与 `.bnf` 是"语言包"，不是代码；改动它们即可支持新语言，主程序零改动。
- 生成的扫描器必须保持零 Qt 依赖，任何需要 Qt 的功能只能放在 `app/` 层。
- **规格歧义（已决策）**：`.` 在当前 Mini-C 中只作为 NUM 的小数点，不单独产生 `SYM_DOT`；`_` 由 ID 正则覆盖，不单列 token。`minic.rules` 保持现状，测试报告需说明，另建 `test_lex_edge.minic` 做词法专项。
- **for-init 双分号（已决策）**：`for-init -> variable-definition` 自带分号，`for-stmt` 又要求一个分号，故语法合法形式类似 `for (int q; ; q < 1; q = q + 1)`。保留 `minic.bnf`，在 `test_edge.minic` 中加注释说明，不擅自修改文法锚点。
- **数组下标左值（T3.2 修复）**：`arr[0] = 1;` 语法可通过，但当前 `IrGenerator` 报“不支持数组下标左值赋值”；T3.2 增加 `[]=` 四元式并更新 `ir_generator_test`。
- **联合流水线严格性（已决策）**：任务三必须校验 `runProducedLex == true`；编译/运行失败时只显示预览并标红，绝不把预览 `sample.lex` 送入语法分析。
- 测试报告与截图需从 `build/` 复制到 `docs/test-reports/`，因为 `build/` 不提交。
