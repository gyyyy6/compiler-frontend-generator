#pragma once

// 中间代码生成器（T2.9，依据 docs/PROGRESS.md T2.9 推导锚点 与 docs/DATA_STRUCTURES.md §5.7）
// 纯 C++17 + STL，不依赖 Qt。
// 输入：化简后 Grammar + ParseTree（T2.8 LrDriver 产出）→ Result<IrProgram>。
// 形式：四元式 (quadruple)；任务书「选做内容」按「选做一律按必做」原则实现。
//
// 设计要点（实现与推导锚点一致，见 PROGRESS.md T2.9 设计点 1~7）：
//   - 操作符挂在非终结符节点上（addop/relop/comparison-op/mulop/powop），
//     从两操作数之间取节点并经 opText 归一：SYM_EQ→==、SYM_NE→!=（TINY `=`/`<>`
//     lexeme 归一，避免与赋值 `=` 冲突）。
//   - 无 else 的 if 不分配结束标签（jfalse 目标标签兼作结束标签，label_seq == 发出的 label 数）。
//   - repeat-until 的 jfalse 跳回 body；while/for 的 jfalse 跳出口（方向相反）。
//   - 前缀 ++/-- 返回自增后值（`k=++i` → `(+ i 1 t)` `(= t i)` `(= t k)`）；
//     后缀 v++/v-- 返回自增前值（先自增、返回旧值）。
//   - 函数头 `label <函数名>`；void 函数 / 末尾无显式 ret 时补隐式 `(ret,,,)`。
//   - factor -> ( expression ) 直接返回内部表达式临时变量，不额外生成四元式。
//
// 失败诊断：stage = "ir"（Severity::Error）：防御性未知产生式 / 树结构异常，一律报告。

#include <string>
#include <vector>

#include "Grammar.h"
#include "ParseTree.h"
#include "Result.h"

namespace compiler::ir {

// 四元式：空字段 = ""。op 白名单见 DATA_STRUCTURES §5.7。
struct Quadruple {
    std::string op;
    std::string arg1;
    std::string arg2;
    std::string result;
};

// 中间代码程序：四元式序列
struct IrProgram {
    std::vector<Quadruple> quads;
};

class IrGenerator {
public:
    // 输入：化简后 Grammar + ParseTree（T2.8 产出）→ Result<IrProgram>。
    // 失败 stage="ir"（防御性未知产生式 / 树结构异常，一律报告）。
    Result<IrProgram> generate(const Grammar& g, const ParseTree& tree) const;
};

} // namespace compiler::ir
