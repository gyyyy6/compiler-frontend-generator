#pragma once

// 正则表达式抽象语法树（T1.3）
// 纯 C++17 + STL，不依赖 Qt。运算符支持见 RegexParser.h 注释；
// 本文件只定义 AST 结构与构造工厂，不含解析/构造算法。
// 命名类（digit/letter 等）在解析期展开为 CharClass/NegatedClass，
// 因此 AST 中不保留 NamedClass 节点。

#include <memory>
#include <string>
#include <vector>

namespace compiler::regex {

// 正则 AST 节点类别
enum class RegexNodeKind : int {
    Empty,          // # 空串（匹配空语言 ε 的字符串）
    Char,           // 单个字符（转义/字面量解析后）
    CharClass,      // 字符类 [abc...]，charClass 为已展开的字符集合（区间已展开）
    NegatedClass,   // 反义字符类 [^...]（或 ~X 折叠的等价物），charClass 为被排除集合
    Dot,            // . 任意字符
    Concat,         // 连接：children 依序全部匹配
    Alternate,      // 选择 |：children 任一匹配
    Star,           // * 闭包：children[0] 零次或多次
    Plus,           // + 正闭包：children[0] 一次或多次
    Optional,       // ? 可选：children[0] 零次或一次
    Negate,         // ~ 非：children[0] 的字符级补集（解析期要求字符级原子）
};

// 正则 AST 节点（值语义外壳 + unique_ptr 子树，移动后持有）
struct RegexNode {
    RegexNodeKind kind = RegexNodeKind::Char;
    char ch = 0;                     // Char 时有效
    std::string charClass;           // CharClass/NegatedClass 时有效（已展开）
    std::vector<std::unique_ptr<RegexNode>> children;  // 依 kind 语义（见上）
};

// ---- 构造工厂（inline）----

inline std::unique_ptr<RegexNode> makeRegexChar(char c) {
    auto n = std::make_unique<RegexNode>();
    n->kind = RegexNodeKind::Char;
    n->ch = c;
    return n;
}

inline std::unique_ptr<RegexNode> makeRegexEmpty() {
    auto n = std::make_unique<RegexNode>();
    n->kind = RegexNodeKind::Empty;
    return n;
}

inline std::unique_ptr<RegexNode> makeRegexClass(std::string chars) {
    auto n = std::make_unique<RegexNode>();
    n->kind = RegexNodeKind::CharClass;
    n->charClass = std::move(chars);
    return n;
}

inline std::unique_ptr<RegexNode> makeRegexNegatedClass(std::string chars) {
    auto n = std::make_unique<RegexNode>();
    n->kind = RegexNodeKind::NegatedClass;
    n->charClass = std::move(chars);
    return n;
}

inline std::unique_ptr<RegexNode> makeRegexDot() {
    auto n = std::make_unique<RegexNode>();
    n->kind = RegexNodeKind::Dot;
    return n;
}

// 单目节点：Star / Plus / Optional / Negate
inline std::unique_ptr<RegexNode> makeRegexUnary(RegexNodeKind kind,
                                                 std::unique_ptr<RegexNode> operand) {
    auto n = std::make_unique<RegexNode>();
    n->kind = kind;
    n->children.push_back(std::move(operand));
    return n;
}

// N 元连接节点（按顺序拼接）
inline std::unique_ptr<RegexNode> makeRegexConcat(
    std::vector<std::unique_ptr<RegexNode>> parts) {
    auto n = std::make_unique<RegexNode>();
    n->kind = RegexNodeKind::Concat;
    n->children = std::move(parts);
    return n;
}

// N 元选择节点（任一匹配）
inline std::unique_ptr<RegexNode> makeRegexAlternate(
    std::vector<std::unique_ptr<RegexNode>> alts) {
    auto n = std::make_unique<RegexNode>();
    n->kind = RegexNodeKind::Alternate;
    n->children = std::move(alts);
    return n;
}

} // namespace compiler::regex
