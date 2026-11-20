#pragma once

// .bnf 文法文件解析器（T2.1，依据 docs/FILE_FORMATS.md §3 与 docs/DATA_STRUCTURES.md §5.1）
// 纯 C++17 + STL，不依赖 Qt。.bnf 文本 → Result<Grammar>。
//
// 解析内容：
//   指令     : @start <nonterminal>（至多一次；缺失时缺省取第一条产生式左部，记 Info）
//   产生式   : lhs -> alt ('|' alt)* ';'；多行续写（换行当空白，'|' 可位于续行行首）
//   符号     : 裸标识符（[A-Za-z_][A-Za-z0-9_-]*）或引号字面量（'...' / "..."）
//   注释     : 行首（前导空白后）'#' 开头 = 行注释；产生式上下文中的 '#' = 空串
//   空串     : '#' 或 'ε'（UTF-8 U+03B5）→ 空候选，Production.rhs = {}（DATA_STRUCTURES §5.1）
//   结束符   : '$' → 内部符号 id 0（SymbolKind::EndOfFile，tokenCode 0）
//   内部符号 : 每次解析固定预置 id 0 = '$'（EndOfFile）、id 1 = '#'（Empty），
//              用户符号自 id 2 起按首次出现顺序分配（DATA_STRUCTURES §5.1）
//   非终结符 : 两遍扫描——左部出现过的符号 = 非终结符；右部出现且未在左部 = 终结符
//   终结符映射: registry 非空时，裸名按 name、引号字面量按 display 查 TokenCodeRegistry
//              （主 A：sample.lex 头部 fromHeader；备 B：重解析 .rules）；
//              查不到 → Error（stage="bnf-parse"，1 基行列号）；registry 为空时 tokenCode 保持 -1
//
// 失败诊断：stage = "bnf-parse"，Diagnostic.loc.line/column 为 1 基。
// 同一实例可重复调用 parse/parseFile，每次调用前内部状态自动重置（reset）。

#include <set>
#include <string>
#include <vector>

#include "Grammar.h"
#include "Result.h"
#include "TokenCodeRegistry.h"

namespace compiler::grammar {

class BnfParser {
public:
    // 解析 .bnf 文本 → Grammar。
    // registry 可选：
    //   非空 → 终结符→tokenCode 解析与校验（裸名按 name、引号字面量按 display），
    //          查不到 → Diagnostic(stage="bnf-parse", Severity::Error) 且 ok=false；
    //   nullptr → 仅构建 Grammar 结构，终结符 tokenCode 保持 -1（无词法表场景/算法调试用）。
    Result<Grammar> parse(const std::string& text,
                          const TokenCodeRegistry* registry = nullptr);

    // 读取文件（UTF-8，去 BOM）后调用 parse；文件不存在/读取失败 → Error（loc.line = -1）。
    Result<Grammar> parseFile(const std::string& path,
                              const TokenCodeRegistry* registry = nullptr);

private:
    // 词法 token 类型（.bnf 专用小词法）
    enum class Tok : int {
        End,        // 文件结束
        Ident,      // 裸标识符：stmt-sequence / KW_IF / ID ...
        Literal,    // 引号字面量：'if' / "if"（text 为去引号内容）
        Arrow,      // ->
        Pipe,       // |
        Semi,       // ;
        Hash,       // #（产生式上下文中的空串记号）
        Epsilon,    // ε（UTF-8 U+03B5）
        Dollar,     // $
        AtStart,    // @start
    };

    struct Token {
        Tok type = Tok::End;
        std::string text;
        SourceLocation loc;
    };

    const Token& cur() const { return tokens_[pos_]; }
    void advance() { if (pos_ + 1 < tokens_.size()) ++pos_; }

    void reset();
    bool tokenize(const std::string& text);            // 字符级切词（含行列号、行首注释消歧）
    bool parseGrammar();                               // 顶层：@start 指令 + 产生式循环
    bool parseDirective();                             // @start <name>
    bool parseProduction();                            // 一条 lhs -> alt ('|' alt)* ';'
    int  resolveRhsSymbol(const Token& tk);            // 右部符号 → Symbol.id；失败 -1
    int  ensureSymbol(const std::string& name, SymbolKind kind,
                      const SourceLocation& loc);      // 查找或登记符号（同名异类 → 错误）
    void addProduction(int lhsId, const std::vector<int>& rhs);
    bool resolveStart();                               // @start 校验 / 缺省取第一条左部
    void addError(const SourceLocation& loc, const std::string& msg);
    void addInfo(const SourceLocation& loc, const std::string& msg);
    Result<Grammar> makeResult();

    std::vector<Token> tokens_;
    size_t pos_ = 0;
    std::vector<Diagnostic> diags_;
    bool failed_ = false;

    Grammar g_;                          // 在构文法（值语义，可移动）
    std::set<std::string> lhsNames_;     // 第一遍收集的左部名字 = 非终结符集合
    bool sawStart_ = false;              // @start 是否出现
    std::string startName_;              // @start 指向的名字
    SourceLocation startLoc_;            // @start 名字位置（诊断用）
    int firstLhs_ = -1;                  // 第一条产生式左部（缺省开始符号）
    SourceLocation firstLhsLoc_;         // 第一条产生式左部位置
    const TokenCodeRegistry* registry_ = nullptr;
};

} // namespace compiler::grammar
