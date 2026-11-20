#pragma once

// 统一结果载体（T1.2，依据 docs/DATA_STRUCTURES.md §8）
// 纯 C++17 + STL，不依赖 Qt。

#include <utility>
#include <vector>

#include "Diagnostic.h"

namespace compiler {

// 结果：ok + 值 + 诊断列表（所有模块统一返回载体）
template <typename T>
struct Result {
    bool ok = false;
    T    value{};
    std::vector<Diagnostic> diagnostics;

    static Result success(T v) { return {true, std::move(v), {}}; }
    static Result failure(Diagnostic d) { return {false, {}, {d}}; }
    void addDiagnostic(Diagnostic d) { diagnostics.push_back(std::move(d)); }
};

} // namespace compiler
