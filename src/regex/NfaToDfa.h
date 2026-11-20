#pragma once

// NFA → DFA 子集构造（T1.4 接口预留）
// 纯 C++17 + STL，不依赖 Qt。T1.3 仅声明、不实现；
// T1.4 提供实现（含 ε-闭包、状态子集记录、接受态 tokenCode/priority 合并）。
// 失败诊断 stage = "dfa-build"。

#include "NFA.h"
#include "DFA.h"
#include "Diagnostic.h"
#include "Result.h"

namespace compiler::regex {

// 子集构造：NFA → DFA。
// DfaState.nfaStates 记录对应 NFA 状态子集（可视化/调试用）；
// Dfa.alphabet 填所有参与转移的字符。T1.4 之前调用方不应调用本函数。
Result<Dfa> subsetConstruct(const Nfa& nfa);

} // namespace compiler::regex
