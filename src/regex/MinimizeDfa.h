#pragma once

// DFA 最小化（T1.5，Hopcroft 算法）
// 纯 C++17 + STL，不依赖 Qt。
// 输入 Dfa，输出接受同一语言的最小化 Dfa。
// 失败诊断 stage = "dfa-minimize"。

#include "DFA.h"
#include "Result.h"

namespace compiler::regex {

// Hopcroft 最小化：Dfa → 最小 Dfa。
// - 初始划分：接受态按 (tokenCode, priority) 分组、非接受态单独一组，
//   保证不同 tokenCode/priority 的等价终态不会合并而改变“报什么 token”的语义；
// - 多个等价终态合并时取 priority 最小者（并列取 tokenCode 最小）的 tokenCode/priority；
// - 保留 startState 与 alphabet；死状态（trap）按普通状态参与划分，不改变语言语义；
// - 接受态为空时直接返回原 DFA。
Result<Dfa> minimizeDfa(const Dfa& dfa);

} // namespace compiler::regex
