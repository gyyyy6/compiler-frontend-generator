#pragma once

// 词法单元数据结构（T1.2，依据 docs/DATA_STRUCTURES.md §3）
// 纯 C++17 + STL，不依赖 Qt。code 为语言相关单词编码，由 TokenCodeRegistry
// 统一分配与管理；0 = EOF，255 = ERROR（保留编码，见 DATA_STRUCTURES.md §4）。

#include <string>

namespace compiler {

// 词法单元类别
enum class TokenKind : int {
    EndOfFile = 0,   // 文件结束
    Keyword,         // 关键字/保留字
    Identifier,      // 标识符
    Number,          // 数值（整数/浮点）
    String,          // 字符串字面量（扩展用）
    Operator,        // 运算符（:=, +, <=, ...）
    Punct,           // 界符（; , ( ) [ ] ...）
    Comment,         // 注释（仅内部阶段用，不写入 sample.lex）
    Error,           // 非法字符
    Unknown,
};

// 源程序位置信息
struct SourceLocation {
    int line   = 1;   // 1 基
    int column = 1;   // 1 基（按字节计，见 FILE_FORMATS.md）
    int offset = 0;   // 文件字节偏移，0 基
};

// 词法单元
struct Token {
    int         code   = 0;        // 语言相关单词编码；0=EOF，255=ERROR
    TokenKind   kind   = TokenKind::Unknown;
    std::string name;              // 编码登记名（与 .rules 中 TokenName 一致），如 "KW_IF"、"ID"
    std::string lexeme;            // 源程序中的实际拼写
    SourceLocation loc;

    bool isError() const { return code == 255; }
    bool isEof()   const { return code == 0; }
};

} // namespace compiler
