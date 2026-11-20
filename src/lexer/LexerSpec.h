#pragma once

// 词法规则规格（T1.6，依据 docs/FILE_FORMATS.md §2 / docs/DATA_STRUCTURES.md §3-4）
// 纯 C++17 + STL，不依赖 Qt。.rules 解析产物：LexRulesParser 产出 LexerSpec，
// 供 T1.7 扫描器 / T1.8 代码生成 / T1.9 可视化消费。
//
// 本结构为 move-only（LexerRule 含 unique_ptr）：所有返回/传参一律移动语义，
// 禁止拷贝；Result<LexerSpec> 随之为 move-only，只能按值移动返回。

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Token.h"               // TokenKind
#include "TokenCodeRegistry.h"   // TokenCodeRegistry
#include "RegexAst.h"            // compiler::regex::RegexNode

namespace compiler::lexer {

// 一条词法规则（已展开为单个单词）。
//
// L 列表（name<code>L=...）在解析期按 '|' 切分后，每一项都走 parseRegex
// （裸正则路径）解析为 AST，而不是"字面量直构 AST"：
//   - Symbol_200L= \+ | - | \* | / | = | < | <= | << 中，\+ / \* 是转义正则
//     写法、<< 是两个字符的连接，均按正则语义解析；
//   - Keyword103L=INT | DOUBLE | BOOL 中，INT 等同样走 parseRegex（裸词按
//     字面字符解析），只是关键字规则最终只登记编码、不保留 AST。
//
// priority 语义（同行 L 项）：
//   - 同一行展开的所有 L 项 priority 相同（= 该行在文件中的顺序，0 基）；
//   - 长度不同的符号（= 与 ==、< 与 <=）靠 T1.7 最长匹配区分，不靠 priority；
//   - 长度相同且同一输入可同时匹配时（仅 L 列表内重复/交叠拼写的退化情况），
//     按 L 列表内声明顺序取最小 priority：合并接受态"priority 并列取最小
//     tokenCode"，而 L 项编码自 <code> 递增，先声明者 tokenCode 更小 → 先声明
//     者胜。
//
// 单词类别由登记名前缀决定（FILE_FORMATS §2.3）：
//   - "Keyword" 前缀 → isKeyword=true：只登记编码，不生成 DFA（由 ID 规则识别
//     后重分类，见 LexerSpec::keywordLookup）；
//   - "Comment" 前缀 → isComment=true：生成 DFA，但扫描输出时丢弃；
//   - 其余 → 普通单词，生成 DFA。
struct LexerRule {
    std::string name;                 // 登记名："KW_IF"、"ID"、"INT"、"+"
    int  code = -1;                   // 最终编码（显式或自动，1..254）
    int  priority = 0;                // 文件顺序（0 基，越小越优先）；同行 L 项相同
    TokenKind kind = TokenKind::Unknown;   // 由登记名推断或解析器显式指定
    bool isComment = false;           // Comment 前缀：生成 DFA、扫描输出时不写入
    bool isKeyword = false;           // Keyword 前缀：只登记编码、不生成 DFA
    std::string pattern;              // 模式原文（展示/调试用；L 项为该字面量原文）
    std::unique_ptr<regex::RegexNode> regex; // parseRegex 解析后的 AST；isKeyword 时为 nullptr

    // 移动语义（含 unique_ptr），禁拷贝
    LexerRule() = default;
    LexerRule(LexerRule&&) noexcept = default;
    LexerRule& operator=(LexerRule&&) noexcept = default;
    LexerRule(const LexerRule&) = delete;
    LexerRule& operator=(const LexerRule&) = delete;
};

// 词法规则规格（.rules 解析产物，贯穿 T1.6-T1.9；move-only，勿拷贝）
struct LexerSpec {
    std::string name;                                        // @name（可选，可为空）
    bool caseSensitive = true;                               // @case-sensitive
    std::set<char> ignoreChars = {' ', '\t', '\r', '\n'};    // @ignore（默认值）
    std::vector<std::pair<std::string, std::string>> blockComments; // @comment block（可多条）
    std::vector<std::string> lineComments;                   // @comment line（可多条）
    std::vector<LexerRule> rules;                            // 展开后的规则，声明顺序 = 优先级
    TokenCodeRegistry registry;                              // 已填充（含 EOF/ERROR 保留项）

    // 关键字反查表：键 = 关键字拼写的 ASCII 小写折叠，值 = 对应编码。
    // T1.7 扫描器识别出 ID 后，把 lexeme 做 ASCII 小写折叠再查本表，命中即
    // 重分类为关键字 token（lexeme 保持原文）；等价于 codeOf(折叠拼写)。
    // 该查找恒为大小写不敏感（与 @case-sensitive 无关）：任务书约定关键字
    // 拼写用大写（INT/DOUBLE/BOOL）、源码关键字为小写（int/double/bool）。
    std::map<std::string, int> keywordLookup;
};

} // namespace compiler::lexer
