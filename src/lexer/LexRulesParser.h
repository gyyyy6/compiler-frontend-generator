#pragma once

// .rules 规则文件解析器（T1.6，依据 docs/FILE_FORMATS.md §2 与任务书输入格式）
// 纯 C++17 + STL，不依赖 Qt。.rules 文本 → Result<LexerSpec>。
//
// 解析内容：
//   指令     : @name / @case-sensitive / @ignore / @comment block|line；# 为注释行。
//   规则行   : 三形式兼容——
//              rule Name = pattern      （rule 前缀可省略，TINY 风格，自动编码）
//              Name_<code> = pattern    （显式编码，如 Comment_102 / Num_100 / ID_101）
//              Name<code> = pattern     （显式编码，无下划线）
//              Name<code>L = list       （L=字面量列表，按 '|' 切分，每项走 parseRegex，
//                                        编码自 <code> 递增；如 Keyword103L / Symbol_200L）
//   命名类   : digit=[0-9] / letter=[A-Za-z] 且名字已在命名类表 → 注册/重定义，不生成 token。
//   编码     : 显式与自动混用；自动分配跳过已用编码（见 TokenCodeRegistry 显式编码重载）。
//   类别     : 名字前缀 "Keyword" → 只登记编码 + 构建 keywordLookup，不生成 DFA（regex=nullptr）；
//              "Comment"         → 生成 DFA，扫描输出时丢弃（isComment=true）。
//   大小写   : @case-sensitive false 时，解析后对每条规则的 AST 做 ASCII 小写折叠。
//
// 失败诊断：stage = "rules-parse"，Diagnostic.loc.line 为 1 基行号。
// 同一实例可重复调用 parse/parseFile，每次调用前内部状态自动重置（reset）。

#include <string>
#include <vector>

#include "LexerSpec.h"
#include "RegexParser.h"   // regex::RegexNameTable（命名类表）
#include "Result.h"

namespace compiler::lexer {

class LexRulesParser {
public:
    // 解析 .rules 文本 → LexerSpec（registry 已填充、rules 已展开、keywordLookup 已构建）。
    // 失败：ok=false，diagnostics 含错误（带 1 基行号）。
    Result<LexerSpec> parse(const std::string& text);

    // 读取文件（UTF-8，去 BOM）后调用 parse。
    Result<LexerSpec> parseFile(const std::string& path);

private:
    void reset();                                  // 清空本轮状态，重建命名类表（digit/letter/eoln/eol）
    bool handleLine(const std::string& line, int lineNo);   // 单行分类与处理；失败返回 false
    bool handleDirective(const std::string& rest, int lineNo);
    bool handleRule(const std::string& rest, int lineNo);   // 规则行（含 L 列表展开）
    void addError(int lineNo, int col, const std::string& msg); // 记一条 stage="rules-parse" 错误

    LexerSpec spec_;               // 在构规格（move-only；parse 末尾 move 出）
    std::vector<Diagnostic> diags_;
    regex::RegexNameTable names_;  // 命名类表：内置 digit/letter + 预注册 eoln/eol
    int ruleIndex_ = 0;            // 规则行顺序（0 基 = priority；命名类行不计数）
    bool failed_ = false;
};

} // namespace compiler::lexer