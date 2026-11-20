#pragma once

// 文法符号数据结构（T1.2，依据 docs/DATA_STRUCTURES.md §5）
// 纯 C++17 + STL，不依赖 Qt。

#include <string>

namespace compiler {

// 文法符号类别
enum class SymbolKind : int {
    Terminal,      // 终结符
    Nonterminal,   // 非终结符
    EndOfFile,     // 结束符 $（分析表内部用）
    Empty,         // 空串 ε / #（内部用）
};

// 文法符号
struct Symbol {
    int  id        = -1;
    std::string name;
    SymbolKind kind = SymbolKind::Terminal;
    int  tokenCode = -1;   // 终结符时：对应词法单词编码；-1 表示无直接对应
};

} // namespace compiler
