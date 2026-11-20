#pragma once

// 诊断信息数据结构（T1.2，依据 docs/DATA_STRUCTURES.md §8）
// 纯 C++17 + STL，不依赖 Qt。所有模块统一错误载体。

#include <string>

#include "Token.h"

namespace compiler {

// 严重级别
enum class Severity : int { Info, Warning, Error };

// 单条诊断
struct Diagnostic {
    Severity severity = Severity::Error;
    std::string stage;       // 产生阶段："rules-parse" / "nfa-build" / "dfa-build"
                             //             / "dfa-min" / "codegen" / "compile" / "scan"
    std::string message;
    SourceLocation loc;      // 无位置时 line = -1
};

} // namespace compiler
