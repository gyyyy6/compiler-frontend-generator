#pragma once

// 文法化简器（T2.2，依据 docs/PROGRESS.md 决策点 3）
// 纯 C++17 + STL，不依赖 Qt。输入 Grammar（T2.1 BnfParser 产出）→ Result<SimplifiedGrammar>。
//
// 化简规则（决策点 3）：
//   1) 删除无用符号/产生式（不可导出终结符串）：
//        产生式删除条件 = 左部非 productive **或** 右部含非 productive 非终结符。
//        例：S -> # | T ; T -> T 中，S 经 S-># 是 productive，但 S->T 引用了非
//        productive 的 T，该产生式必须一并删除，否则留下悬垂引用。
//   2) 删除不可达符号/产生式（从 startSymbol BFS）。
//   3) 不做单位产生式消除（避免改动产生式 id，影响 T2.8 语法树）。
//   4) ε 产生式一律保留（rhs 为空 → 左部直接 productive）。
//
// 产物：Grammar 的符号/产生式 id 重编号为稠密（保留 $ = id 0、# = id 1 固定槽，
//       T2.4 起可依赖该内部约定）；SimplifyLog 记录删除动作，其中
//       removedProductions / removedSymbols 分别按"先 useless 后 unreachable、
//       各自按原始 id 升序"记录；removedProductions 记输入 Grammar 的原始 id。
//       removedSymbols 仅记录被删的**非终结符**（孤儿终结符虽从符号表移除，但不单列日志）。
//
// 前置条件：输入 Grammar 满足 BnfParser 不变式 symbols[i].id == i、
//           productions[i].id == i（BnfParser 保证；手工构造测试文法须遵守）。
//
// 失败诊断：stage = "simplify"（Severity::Error）。触发条件：
//   - startSymbol 缺失（startSymbol < 0 或越界）
//   - 产生式左部/右部引用了不存在的符号 id（防御，BnfParser 不会产生）
//   - startSymbol 不可导出终结符串 → 化简后文法为空

#include <string>
#include <vector>

#include "Grammar.h"
#include "Result.h"

namespace compiler::grammar {

// 一条被删符号记录
struct RemovedSymbol {
    std::string name;      // 符号名（非终结符）
    std::string reason;    // "useless"（不可导出终结符串）/ "unreachable"（从 startSymbol 不可达）
};

// 一条被删产生式记录
struct RemovedProduction {
    int         originalId = -1;   // 输入 Grammar 中的原始产生式 id
    std::string reason;            // "useless" / "unreachable"
};

// 化简动作日志（供 GUI 展示：一行一条删除动作）
struct SimplifyLog {
    std::vector<RemovedSymbol>     removedSymbols;      // 先 useless，后 unreachable，各自按原始 id 升序
    std::vector<RemovedProduction> removedProductions;  // 先 useless，后 unreachable，各自按原始 id 升序
    std::string summary;                                // 人类可读摘要；无删除时为"文法已是最简"

    bool empty() const
    {
        return removedSymbols.empty() && removedProductions.empty();
    }
};

// 化简产物 = 新 Grammar + 动作日志
struct SimplifiedGrammar {
    Grammar     grammar;
    SimplifyLog log;
};

class GrammarSimplifier {
public:
    // 化简：删除无用与不可达符号/产生式；ε 保留；不做单位产生式消除。
    Result<SimplifiedGrammar> simplify(const Grammar& g) const;
};

} // namespace compiler::grammar
