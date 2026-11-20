#pragma once

// First / Follow / Nullable 集合计算（T2.3，依据 docs/PROGRESS.md 决策点 与 docs/DATA_STRUCTURES.md §5.2）
// 纯 C++17 + STL，不依赖 Qt。输入化简后 Grammar（T2.2 产出）→ Result<FirstFollowSets>。
//
// 语义约定（DATA_STRUCTURES §5.2）：
//   - ε 在 First 集合中以内部符号 #（Empty，id = kEpsilonSymbolId）表示；
//     First(X) 含 kEpsilonSymbolId ⟺ X nullable。
//   - First($) = {$}（id 0）；First(#) = {#}（id 1）；nullable(#)=true、nullable($)=false。
//   - Follow(startSymbol) 恒含 $（id 0）；Follow 集合按定义不含 ε（#）。
//   - first/follow/nullable 均按符号 id 索引，长度 == Grammar::symbols.size()。
//
// 算法（不动点，自底向上至无变化；实现顺序 nullable → First → Follow）：
//   nullable(X) : X->ε（rhs 空）或 X->Y1..Yn 且全部 Yi nullable。
//   First(X)    : 终结符/$/# 预置；X->Y1..Yn 时逐个加 First(Yi)\{ε}，Yi nullable 则继续，
//                 全部 nullable 时于末尾加 ε（kEpsilonSymbolId）。
//   Follow(A)   : A->αBβ 时加 First(β)\{ε}；β 全 nullable（含 β 空）时加 Follow(A)；
//                 初始 Follow(startSymbol)={$}。链式传播（A->B）经不动点自然收敛。
//   First(α)    : firstOfString，见 static 函数注释（T2.5 lookahead 用）。
//
// 失败诊断：stage = "first-follow"（Severity::Error）：
//   - 产生式为空 / startSymbol 无效 / 产生式引用不存在的符号 id（防御）。
//
// 前置条件：输入 Grammar 满足 BnfParser 不变式 symbols[i].id == i、productions[i].id == i。

#include <set>
#include <string>
#include <vector>

#include "Grammar.h"
#include "Result.h"

namespace compiler::grammar {

// ε 记号 = 内部符号 #（Empty）的 id（DATA_STRUCTURES §5.1 固定为 1）
inline constexpr int kEpsilonSymbolId = 1;

// First / Follow / Nullable 集合（T2.3 产出，T2.5 lookahead 消费）
struct FirstFollowSets {
    std::vector<std::set<int>> first;      // 按符号 id 索引（含 $=0、#=1；first.size()==symbols.size()）
    std::vector<std::set<int>> follow;     // 按符号 id 索引（仅非终结符有意义；其余恒空）
    std::vector<bool>          nullable;   // 按符号 id 索引
};

class FirstFollow {
public:
    // 计算 nullable / First / Follow（不动点），输入任意良构 Grammar（通常为 T2.2 化简产物）。
    Result<FirstFollowSets> compute(const Grammar& g) const;

    // FIRST(α)：α 为符号 id 序列（T2.5 lookahead 用：调用方把 lookahead 终结符追加在 β 尾部）。
    //   逐符号加 First(Yi)\{ε}，Yi nullable 则继续；全部 nullable 时末尾加 ε（id 1）；空串 → {#}。
    //   越界符号 id 防御性忽略。
    static std::set<int> firstOfString(const Grammar& g, const FirstFollowSets& sets,
                                       const std::vector<int>& symbols);
};

} // namespace compiler::grammar
