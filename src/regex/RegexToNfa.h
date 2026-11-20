#pragma once

// 正则 AST → NFA（Thompson 构造，T1.3）
// 纯 C++17 + STL，不依赖 Qt。输入 RegexNode AST，输出 core::Nfa
// （compiler 命名空间，定义见 src/core/NFA.h）。
// 失败诊断 stage = "nfa-build"。

#include <string>

#include "RegexAst.h"
#include "RegexParser.h"
#include "NFA.h"
#include "Diagnostic.h"
#include "Result.h"

namespace compiler::regex {

// Thompson 构造：AST → NFA。
// 结果含唯一接受态；CharClass/NegatedClass 以边形式保留，
// 供 T1.4 子集构造按字母表展开。
Result<Nfa> buildNfa(const RegexNode& ast);

// 便捷入口：正则模式串 → 解析 → Thompson 构造 NFA（一步完成）。
// names 缺省使用内置 digit/letter。
Result<Nfa> buildNfaFromRegex(
    const std::string& pattern,
    const RegexNameTable& names = RegexNameTable{});

} // namespace compiler::regex
