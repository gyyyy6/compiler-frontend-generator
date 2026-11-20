#pragma once

// 正则表达式解析器（T1.3）
// 纯 C++17 + STL，不依赖 Qt。解析正则模式 → RegexNode AST。
//
// 支持的运算符（优先级从高到低）：
//   \        转义（\n \t \r \\ \d \w \s \D \W \S 及任意字符转义为字面量）
//   ~        非（字符级补集，前缀，作用于紧随的字符级原子）
//   ()       分组
//   []       字符类 / 区间（[^...] 为反义类）
//   * + ?    闭包 / 正闭包 / 可选（后缀，绑定其左侧原子）
//   连接     相邻原子直接拼接
//   |        选择（最低优先级）
//   #        空串；. 任意字符；命名类（digit、letter 等）宏替换
//
// 语法（EBNF 简化）：
//   regex       := alternation
//   alternation := concat ('|' concat)*
//   concat      := repetition*
//   repetition  := atom ('*' | '+' | '?')?
//   atom        := '#' | '.' | char | class | '(' regex ')' | '~' atom | name
//   class       := '[' '^'? (char | range)* ']'
//   range       := char '-' char
//   name        := letter (letter | digit | '_')*   // 命名类宏替换
//
// 约定：
//   - 已知命名类（digit/letter 等）宏替换为字符集合；未知标识符按字面
//     字符逐个解析（如 a、ab、a*），保证字面量语义正确。
//   - 字符类外的空白（空格/Tab/CR/LF）作为原子分隔符被忽略；
//     匹配字面空格请用 \s 或 [ ]。
//
// 解析错误通过 core::Result 返回，Diagnostic.stage = "regex-parse"；
// loc.column/loc.offset 指向模式串内的出错位置（1 基 / 0 基）。

#include <map>
#include <memory>
#include <string>

#include "RegexAst.h"
#include "Diagnostic.h"
#include "Result.h"

namespace compiler::regex {

// 命名类表：name → 字符集合（宏替换）
// 内置：digit = [0-9]，letter = [a-zA-Z]；可用 add() 注册更多命名类。
class RegexNameTable {
public:
    RegexNameTable();   // 注册内置 digit / letter

    // 注册/覆盖命名类（chars 为已展开的字符集合）
    void add(const std::string& name, std::string chars);

    bool contains(const std::string& name) const;

    // 不存在返回 nullptr
    const std::string* charsOf(const std::string& name) const;

private:
    std::map<std::string, std::string> classes_;
};

// 解析正则模式 → AST；失败时 ok=false 且 diagnostics 含错误（带位置）。
// names 缺省使用内置 digit/letter。
Result<std::unique_ptr<RegexNode>> parseRegex(
    const std::string& pattern,
    const RegexNameTable& names = RegexNameTable{});

} // namespace compiler::regex
