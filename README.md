# 配置驱动的编译器前端自动生成系统

> 输入一份语言包（`*.rules` + `*.bnf`），自动生成**可独立编译的 C++ 扫描器**与 **LALR(1) 语法分析器**，再输出语法树与四元式中间代码。
> 主程序不含任何关键字、产生式硬编码——换一门语言只需替换语言包。

C++17 / CMake / Qt6 Widgets　·　约 1.44 万行源码 + 约 0.54 万行单元测试（10 个模块）

---

## 1. 它解决什么问题

教学用的编译原理课设通常只针对一种示例语言写死词法与文法。本项目把"语言"抽成**外部数据**：

```
语言包 (.rules/.bnf)
      │
      ├─ 词法链：正则 → Thompson NFA → 子集构造 → Hopcroft 最小化 → 渲染 → 生成独立 C++ 扫描器 → 编译运行
      │
      └─ 语法链：BNF → 无用符号/不可达符号化简 → FIRST/FOLLOW → LR(0) 项目集 → LALR(1) 合并
                 → ACTION/GOTO 表 → 移进-归约 → 语法树 → 四元式
```

两条链都由同一份语言包驱动，GUI 里"一键联合测试"即可跑完整个前端。

## 2. 实测结果（两套内置语言包）

| 指标 | TINY | Mini-C |
|---|---|---|
| 词法规则数 | 26（18 条进自动机） | 35（26 条进自动机） |
| NFA 状态 | 121 | 165 |
| DFA 状态（最小化前 → 后） | 52 → **50** | 70 → **66** |
| 扫描验证 | 105 个单词零错误 | 416 个单词零错误 |
| 化简后产生式 | 37 | 78 |
| LALR(1) 状态 | 59 | 128 |
| 分析表冲突 | 0 | 1（悬垂 `else`，唯一且符合预期） |
| 四元式条数 | 41 | 139 |
| 语法树规模 | 331 节点 / 深度 26 | 1370 节点 / 深度 51 |

两套语言包的状态数与四元式条数都已作为**测试锚点**写进单元测试：任何一条产生式被误删或状态合并出错，测试立即失败。

## 3. 目录结构

```
src/
  core/      基础设施、日志、错误模型
  regex/     正则解析 → NFA → DFA → 最小化
  lexer/     规则装配、最长匹配扫描
  codegen/   由最小 DFA 渲染出独立可编译的 C++ 扫描器源码
  grammar/   BNF 解析与文法化简（无用符号、不可达符号、ε 产生式）
  parser/    LR(0) 项目集、LALR(1) 合并、分析表构造、移进-归约
  ir/        语法树 → 四元式
  pipeline/  词法链 / 语法链 / 联合流水线编排（零 Qt，便于单测）
  gui/       Qt6 界面（四个 Tab：源程序、词法、语法、联合报告）
  app/       程序入口（GUI 与 --minic CLI）
test/        单元测试 + 两套语言的测试语料
docs/        ARCHITECTURE / DATA_STRUCTURES / FILE_FORMATS / REPORT 等设计文档
```

**关键设计**：`pipeline` 与 `codegen` 不依赖 Qt，"生成扫描器 → 编译 → 运行 → 送进语法分析"整条链可在无界面环境跑通并被单测覆盖；Qt 只出现在 `gui/` 与 `app/` 两层。

## 4. 构建与运行

依赖：CMake ≥ 3.16、支持 C++17 的编译器、Qt6 Widgets（兼容 Qt5）。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# GUI
./build/app/CompilerCourseApp.exe

# 命令行联合测试（Mini-C）：词法 → 语法树 → 四元式，产物写入 build/generated/MiniC/
./build/app/CompilerCourseApp.exe --minic test/languages/minic
```

退出码：`0` 全链路成功；`3` 必需阶段失败（仍写报告）；`5` 四元式生成失败。

```bash
# 单元测试
ctest --test-dir build --output-on-failure
```

## 5. 换成自己的语言

1. 在 `test/languages/<你的语言>/` 下写 `<lang>.rules`（词法，支持正则与关键字表）和 `<lang>.bnf`（文法）；
2. 放一份样例源码到 `samples/`；
3. 重新跑联合测试即可，**不需要改一行主程序代码**。

语言包格式见 `docs/FILE_FORMATS.md`，整体设计取舍见 `docs/ARCHITECTURE.md`，完整实验数据见 `docs/REPORT.md`。

---

课程设计项目 · 华南师范大学 计算机学院 计算机科学与技术
