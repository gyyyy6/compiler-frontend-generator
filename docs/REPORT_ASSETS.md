# 报告配图规范与重命名对照表（v1）

> 用途：统一 `docs/prtsc/` 下的截图命名，明确每张图在报告中的位置，并标出因代码更新而失效、需要重截的文件。
> 数据基线见 `REPORT_OUTLINE.md`「实测数据基线」；本文的图号**取代** `REPORT_OUTLINE.md` 中原有的图表规划。

## 1. 命名规范

格式：`<任务前缀>_<语言>_<产物>[_<限定>].png`

- 全小写、下划线分隔、不使用中文与空格；扩展名统一 `.png`。
- **任务前缀**：`t0` 系统总览 / `t1` 任务一词法 / `t2` 任务二语法 / `t3` 任务三联合。
- **语言**：`tiny`、`minic`；纯跨语言或系统级图用 `all` 或省略。
- **产物词表**（只用下列词，保证可检索）：

| 词 | 对应界面区域 |
| --- | --- |
| `mainwindow` | 主窗口全貌（三 Tab + 日志 Dock） |
| `rules` | 正则表达式 / 规则文件编辑页 |
| `nfa` | NFA 状态表 |
| `dfa` | 未最小化的 DFA 状态表 |
| `mindfa` | 最小 DFA 状态表 |
| `scanner` | 生成的词法分析源程序（`generated_lexer.cpp`） |
| `lex` | 单词编码文件 `sample.lex` |
| `unit` | 单元测试 / ctest 运行输出（终端截图） |
| `bnf` | BNF 文法页 |
| `simplified` | 化简后文法 |
| `firstfollow` | FIRST / FOLLOW 表 |
| `itemset` | LR(0) / LALR(1) 项目集 |
| `parsetable` | ACTION / GOTO 分析表 |
| `treeir` | 语法树 + 四元式 IR 同屏（任务二语法树页） |
| `source` `report` | 任务三的源程序+覆盖矩阵 / 联合测试报告 |

- 约定：**一张图只表达一个结论**。同一页面若需分别说明"分析表"和"冲突清单"，用 `_限定` 后缀区分（如 `t2_minic_parsetable_conflict.png`）。
- 图号（图 2-6 等）由正文决定，**不进文件名**，避免后期插入图时批量改名。

## 2. 必需配图清单（报告将引用）

> 进度更新（2026-11-03 23:15）：19 张截图全部按定名归档于 `docs/prtsc/`，含作者补截的 9 张与任务三 4 张；"判定"列不再区分"需重截/待截"，统一为"已归档"。**图号列已与 `docs/REPORT.md` 正文逐条核对一致**（图号由正文决定，本节仅作登记）。

| 新文件名 | 图号 | 内容 | 判定 |
| --- | --- | --- | --- |
| `t0_mainwindow.png` | 图 1-3 | 系统主界面：三 Tab + 日志 Dock + 任务一规则页 | 已归档 |
| `t1_tiny_nfa.png` | 图 2-3 | TINY NFA 状态表（121 态） | 已归档 |
| `t1_tiny_mindfa.png` | 图 2-6 | TINY 最小 DFA 表（52 → 50） | 已归档 |
| `t1_tiny_scanner.png` | 图 2-9 | 生成的词法分析源程序（`generated_lexer.cpp` 头部） | 已归档 |
| `t1_tiny_lex.png` | 图 2-10 | TINY `sample.lex`（头部 + 数据区） | 已归档 |
| `t1_minic_mindfa.png` | 图 2-7 | Mini-C 最小 DFA 表（70 → 66） | 已归档 |
| `t1_unit_test.png` | 图 2-11 | `ctest` 全部 13 项通过 | 已归档 |
| `t2_tiny_bnf.png` | 图 3-2 | TINY BNF 文法页（37 条产生式 / 45 个符号） | 已归档 |
| `t2_tiny_simplified.png` | 图 3-3 | TINY 化简后文法（37 条，带产生式 id） | 已归档 |
| `t2_tiny_firstfollow.png` | 图 3-4 | TINY FIRST / FOLLOW 表 | 已归档 |
| `t2_tiny_itemset.png` | 图 3-6 | TINY LR(0) / LALR(1) 项目集（59 态） | 已归档 |
| `t2_tiny_parsetable.png` | 图 3-9 | TINY ACTION / GOTO（59 态，0 冲突） | 已归档 |
| `t2_tiny_treeir.png` | 图 3-12 | TINY 语法树（331 节点 / 深度 26）+ 41 条四元式 | 已归档 |
| `t2_minic_parsetable.png` | 图 3-10 | Mini-C 分析表，s115 / `KW_ELSE` 冲突单元格高亮 | 已归档 |
| `t2_minic_treeir.png` | 图 4-3 | Mini-C 语法树（1370 节点 / 深度 52）+ 139 条四元式 | 已归档 |
| `t3_source.png` | 图 5-2 | 任务三 · 源程序与覆盖矩阵 | 已归档 |
| `t3_lex.png` | 图 5-3 | 任务三 · 词法结果（416 token / 0 ERROR） | 已归档 |
| `t3_parse.png` | 图 5-4 | 任务三 · 语法结果（128 态 / 1 冲突 / 1370 节点 / 139 四元式） | 已归档 |
| `t3_report.png` | 图 5-5 | 任务三 · 联合测试报告（16 行阶段时间线） | 已归档 |

共 19 张截图；另有 15 幅 Mermaid 流程图由正文代码块直接生成，不占用截图文件名，其编号为：图 1-1、1-2；图 2-1、2-2、2-4、2-5、2-8；图 3-1、3-5、3-7、3-8、3-11；图 4-1、4-2；图 5-1。可见每章内部是"流程图与截图交替编号"，而非"流程图集中在前、截图集中在后"。

## 3. 重命名对照表

> 判定说明：`可用` = 内容与当前构建一致，改名即可；`需重截` = 图中数字与当前代码不符，改名后用新截图覆盖；`归档` = 报告不引用，可移入 `docs/prtsc/_archive/`。
>
> **历史说明**：本节判定列记录的是 2026-10-07 首次截图时的状态。需重截的 8 张已于 2026-11-03 补截并覆盖，当前实际状态以第 2 节"已归档"为准；本节保留原判定，用于追溯每张图当初为什么会失效。

### 3.1 任务一（prtsc1–12，2026-10-07 截取）

| 原文件 | 新文件名 | 报告图号 | 判定 | 失效原因 |
| --- | --- | --- | --- | --- |
| `prtsc1.png` | `t0_mainwindow.png` | 图 1-3 | 需重截 | 日志显示 NFA 127 态，当前 121 态 |
| `prtsc2.png` | `t1_tiny_nfa.png` | 图 2-3 | 需重截 | 同上，NFA 状态数与正文不符 |
| `prtsc3.png` | `t1_tiny_dfa.png` | 不编号 | 归档 | DFA 52 态虽未变，但同屏日志已过期；正文只放最小 DFA |
| `prtsc4.png` | `t1_tiny_mindfa.png` | 图 2-6 | 需重截 | `52 → 50` 与当前一致，但同屏日志 127 态过期 |
| `prtsc5.png` | `t1_tiny_scanner.png` | 图 2-9 | 需重截 | 生成源码内嵌的状态数与当前 50 态一致，同屏日志过期 |
| `prtsc6.png` | `t1_tiny_lex.png` | 图 2-10 | 需重截 | 头部 `token-count: 26` 与当前登记项 28（含 EOF/ERROR）不一致 |
| `prtsc7.png` | `t1_minic_rules.png` | 不编号 | 归档 | 规则页内容有效，但正文以 TINY 为例，无需第二语言同款 |
| `prtsc8.png` | `t1_minic_nfa.png` | 不编号 | 归档 | NFA 状态数与当前 165 不符（旧构建） |
| `prtsc9.png` | `t1_minic_dfa.png` | 不编号 | 归档 | 同上，且正文只放最小 DFA |
| `prtsc10.png` | `t1_minic_mindfa.png` | 图 2-7 | 需重截 | `70 → 66` 与当前一致（可作证据），但同屏含旧 token 数 234 |
| `prtsc11.png` | `t1_minic_scanner.png` | 不编号 | 归档 | 生成源码可用性由 TINY 图代表，且旧构建状态数不同 |
| `prtsc12.png` | `t1_minic_lex.png` | 不编号 | 归档 | 头部字段语义与当前输出不同（`token-count: 33` vs 35） |

### 3.2 任务二（prtsc13–24）

| 原文件 | 新文件名 | 报告图号 | 判定 | 失效原因 |
| --- | --- | --- | --- | --- |
| `prtsc13.png` | `t2_tiny_bnf.png` | 图 3-2 | 可用 | 104 token / 37 产生式 / 59 态均与当前一致 |
| `prtsc14.png` | `t2_tiny_simplified.png` | 图 3-3 | 可用 | 同上 |
| `prtsc15.png` | `t2_tiny_firstfollow.png` | 图 3-4 | 可用 | 文法未改，FIRST/FOLLOW 结果不变 |
| `prtsc16.png` | `t2_tiny_itemset.png` | 图 3-6 | 可用 | 59 态与当前一致 |
| `prtsc17.png` | `t2_tiny_parsetable.png` | 图 3-9 | 可用 | 0 冲突，锚点格不变 |
| `prtsc18.png` | `t2_tiny_treeir.png` | 图 3-12 | 可用 | 331 节点 / 104 叶 / 深度 26 / 41 四元式，与当前一致 |
| `prtsc19.png` | `t2_minic_bnf.png` | 不编号 | 归档 | 旧 `test.minic`（234 token），与当前 416 不符 |
| `prtsc20.png` | `t2_minic_simplified.png` | 不编号 | 归档 | 同上；78 条产生式虽未变，日志 token 数过期 |
| `prtsc21.png` | `t2_minic_firstfollow.png` | 不编号 | 归档 | 同上 |
| `prtsc22.png` | `t2_minic_itemset.png` | 不编号 | 归档 | 128 态未变，日志过期；冲突高亮改由分析表页承担 |
| `prtsc23.png` | `t2_minic_parsetable.png` | 图 3-10 | 需重截 | 日志显示 234 token / 75 quads，当前 416 / 139 |
| `prtsc24.png` | `t2_minic_treeir.png` | 图 4-3 | 需重截 | 树规模与 IR 条数为旧用例（75 条），当前 139 条 |

### 3.3 任务三（`task3/` 子目录）

| 原文件 | 新文件名 | 报告图号 | 判定 | 说明 |
| --- | --- | --- | --- | --- |
| `task3/01_task3_source.png` | `t3_source.png` | 图 5-2 | 可用 | 2026-11-02 当前构建，源程序与覆盖矩阵齐全 |
| `task3/02_task3_lex.png` | `t3_lex.png` | 图 5-3 | 可用 | 416 token / 0 ERROR，与正文一致 |
| `task3/03_task3_parse.png` | `t3_parse.png` | 图 5-4 | 可用 | 128 态 / 1 冲突 / 139 四元式 |
| `task3/04_task3_report.png` | `t3_report.png` | 图 5-5 | 可用 | 阶段时间线 16 行全绿 |

## 4. 重截操作清单（共 8 张 + 1 张新增）

> 本节已于 2026-11-03 全部执行完毕，保留作为操作记录与复现步骤（换机器/重装环境时可照此重拍）。

统一前置：用当前 `develop` 分支的最新构建启动 GUI，窗口最大化，保持日志 Dock 展开，使日志里的阶段数字与正文一致。

| 序号 | 文件 | 操作路径 |
| --- | --- | --- |
| 1 | `t0_mainwindow.png` | 任务一 Tab，打开 `test/languages/tiny` 语言包与 `samples/test.tiny`，点「一键运行」，停在「正则表达式」子 Tab |
| 2 | `t1_tiny_nfa.png` | 同上状态切到「NFA」子 Tab，滚到首行，确认日志为 121 态 |
| 3 | `t1_tiny_mindfa.png` | 切「最小DFA」子 Tab，确认日志 `52 -> 50` |
| 4 | `t1_tiny_scanner.png` | 切「词法源程序」子 Tab，显示文件头部（注释 + `ACC[]` 起始） |
| 5 | `t1_tiny_lex.png` | 切「单词编码」子 Tab，显示 `#file-format` 头部与数据区前几行 |
| 6 | `t1_minic_mindfa.png` | 切到 Mini-C 语言包，一键运行后停在「最小DFA」，确认日志 `70 -> 66` |
| 7 | `t2_minic_parsetable.png` | 任务二 Tab，Mini-C 语言包 + 当前 `sample.lex`，一键运行，切「分析表」子 Tab，滚到状态 115 / `KW_ELSE` 冲突格 |
| 8 | `t2_minic_treeir.png` | 同状态切「语法树」子 Tab，显示根 `program` 与下方四元式区（139 条） |
| 9 | `t1_unit_test.png` | 终端执行 `ctest --test-dir build --output-on-failure`，截末尾 `100% tests passed, 0 tests failed out of 13` |

补充建议：Mini-C 词法若也想留证（正文不引用），按 6 的同一状态切「词法源程序」存为 `t1_minic_scanner.png` 即可。

## 5. 批量重命名命令（可选）

在 `docs/prtsc/` 下执行（Git Bash；PowerShell 把 `mv` 换成 `Move-Item`）：

```bash
mkdir -p _archive
mv prtsc1.png  t0_mainwindow.png
mv prtsc2.png  t1_tiny_nfa.png
mv prtsc3.png  t1_tiny_dfa.png
mv prtsc4.png  t1_tiny_mindfa.png
mv prtsc5.png  t1_tiny_scanner.png
mv prtsc6.png  t1_tiny_lex.png
mv prtsc7.png  t1_minic_rules.png
mv prtsc8.png  t1_minic_nfa.png
mv prtsc9.png  t1_minic_dfa.png
mv prtsc10.png t1_minic_mindfa.png
mv prtsc11.png t1_minic_scanner.png
mv prtsc12.png t1_minic_lex.png
mv prtsc13.png t2_tiny_bnf.png
mv prtsc14.png t2_tiny_simplified.png
mv prtsc15.png t2_tiny_firstfollow.png
mv prtsc16.png t2_tiny_itemset.png
mv prtsc17.png t2_tiny_parsetable.png
mv prtsc18.png t2_tiny_treeir.png
mv prtsc19.png t2_minic_bnf.png
mv prtsc20.png t2_minic_simplified.png
mv prtsc21.png t2_minic_firstfollow.png
mv prtsc22.png t2_minic_itemset.png
mv prtsc23.png t2_minic_parsetable.png
mv prtsc24.png t2_minic_treeir.png
mv task3/01_task3_source.png t3_source.png
mv task3/02_task3_lex.png    t3_lex.png
mv task3/03_task3_parse.png  t3_parse.png
mv task3/04_task3_report.png t3_report.png
rmdir task3
# 归档（报告不引用）
mv t1_tiny_dfa.png t1_minic_rules.png t1_minic_nfa.png t1_minic_dfa.png \
   t1_minic_scanner.png t1_minic_lex.png t2_minic_bnf.png t2_minic_simplified.png \
   t2_minic_firstfollow.png t2_minic_itemset.png _archive/
```

## 6. 正文引用约定

- 图题格式：`图 2-3 TINY 语言 NFA 状态表（121 个状态）`，图题在上、图在下，图下一行注明来源路径：`（截图：docs/prtsc/t1_tiny_nfa.png）`。
- 正文引用统一写「如图 2-6 所示」，不写文件名；文件名只出现在图注的来源说明里。
- 图号与文件的对应关系以本文第 2 节为准；若后续增图，先在此登记再写正文。
