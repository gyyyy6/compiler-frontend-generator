// .bnf 文法文件解析器实现（T2.1，依据 docs/FILE_FORMATS.md §3 与 docs/DATA_STRUCTURES.md §5.1）
//
// 实现要点：
//   - 两遍扫描：先收集全部左部名字（= 非终结符集合），再自上而下解析产生式；
//   - 行首 '#'（仅前导空白）为行注释，产生式上下文中的 '#' 为空串记号；
//   - 换行视为空白 → 天然支持多行续写（'|' 可位于续行行首）；
//   - 产生式右部为空（除显式 '#'/'ε'）一律报错，避免隐式 ε 造成歧义；
//   - 终结符映射由调用方提供的 TokenCodeRegistry 完成（主 A：sample.lex 头部
//     fromHeader；备 B：重解析 .rules），本解析器只依赖 core。
//
// 失败诊断：stage = "bnf-parse"，Diagnostic.loc 为 1 基行列号。

#include "BnfParser.h"

#include <fstream>
#include <iterator>
#include <utility>

namespace compiler::grammar {

namespace {

// 标识符首字符：字母或下划线
bool isIdentStart(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

// 标识符续字符：字母/数字/下划线/连字符
//（'-' 后跟 '>' 时属于 "->" 箭头，在切词循环里终止标识符）
bool isIdentChar(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-';
}

} // namespace

void BnfParser::reset()
{
    tokens_.clear();
    pos_ = 0;
    diags_.clear();
    failed_ = false;
    g_ = Grammar{};
    lhsNames_.clear();
    sawStart_ = false;
    startName_.clear();
    startLoc_ = SourceLocation{};
    firstLhs_ = -1;
    firstLhsLoc_ = SourceLocation{};
    registry_ = nullptr;
}

bool BnfParser::tokenize(const std::string& text)
{
    tokens_.clear();
    const size_t n = text.size();
    size_t i = 0;

    // 去 UTF-8 BOM
    if (n >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        i = 3;
    }

    int line = 1;
    int col = 1;
    bool lineHasContent = false; // 本行是否已出现非空白内容（决定 '#' 是否为行注释）

    while (i < n) {
        const char c = text[i];

        // 空白（含换行）：CRLF/LF 兼容
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (c == '\n') {
                ++line;
                col = 1;
                lineHasContent = false;
            } else {
                ++col;
            }
            ++i;
            continue;
        }

        const SourceLocation loc{line, col, static_cast<int>(i)};

        // 行注释：'#' 且本行尚无其他内容（跳过至行尾）
        if (c == '#' && !lineHasContent) {
            while (i < n && text[i] != '\n') {
                ++i;
                ++col;
            }
            continue;
        }

        lineHasContent = true;

        // 指令 @start
        if (c == '@') {
            if (text.compare(i, 6, "@start") == 0) {
                const char after = (i + 6 < n) ? text[i + 6] : '\0';
                if (after == ' ' || after == '\t' || after == '\r' ||
                    after == '\n' || after == '\0') {
                    tokens_.push_back({Tok::AtStart, "start", loc});
                    for (int k = 0; k < 6; ++k) {
                        ++i;
                        ++col;
                    }
                    continue;
                }
            }
            addError(loc, "未知指令：.bnf 仅支持 @start");
            ++i;
            ++col;
            continue;
        }

        // 箭头 ->
        if (c == '-' && i + 1 < n && text[i + 1] == '>') {
            tokens_.push_back({Tok::Arrow, "->", loc});
            i += 2;
            col += 2;
            continue;
        }

        // 单字符符号
        if (c == '|') {
            tokens_.push_back({Tok::Pipe, "|", loc});
            ++i;
            ++col;
            continue;
        }
        if (c == ';') {
            tokens_.push_back({Tok::Semi, ";", loc});
            ++i;
            ++col;
            continue;
        }
        if (c == '$') {
            tokens_.push_back({Tok::Dollar, "$", loc});
            ++i;
            ++col;
            continue;
        }
        if (c == '#') {
            tokens_.push_back({Tok::Hash, "#", loc});
            ++i;
            ++col;
            continue;
        }

        // ε（UTF-8：U+03B5 = 0xCE 0xB5）
        if (static_cast<unsigned char>(c) == 0xCE && i + 1 < n &&
            static_cast<unsigned char>(text[i + 1]) == 0xB5) {
            tokens_.push_back({Tok::Epsilon, "\xCE\xB5", loc});
            i += 2;
            col += 2;
            continue;
        }

        // 引号字面量：'...' / "..."（不含转义；未闭合或跨行 → 错误）
        if (c == '\'' || c == '"') {
            const char q = c;
            ++i;
            ++col;
            std::string content;
            bool closed = false;
            while (i < n) {
                if (text[i] == q) {
                    closed = true;
                    ++i;
                    ++col;
                    break;
                }
                if (text[i] == '\n') {
                    break; // 未闭合
                }
                content.push_back(text[i]);
                ++i;
                ++col;
            }
            if (!closed) {
                addError(loc, "引号字面量未闭合");
                continue;
            }
            tokens_.push_back({Tok::Literal, content, loc});
            continue;
        }

        // 裸标识符
        if (isIdentStart(c)) {
            std::string name;
            while (i < n && isIdentChar(text[i])) {
                // '-' 后跟 '>' 时终止：属于 "->" 箭头
                //（兼容 "stmt-sequence->x" 无空格写法）
                if (text[i] == '-' && i + 1 < n && text[i + 1] == '>') {
                    break;
                }
                name.push_back(text[i]);
                ++i;
                ++col;
            }
            tokens_.push_back({Tok::Ident, name, loc});
            continue;
        }

        addError(loc, std::string("非法字符 '") + c + "'");
        ++i;
        ++col;
    }

    tokens_.push_back({Tok::End, "", {line, col, static_cast<int>(i)}});
    return !failed_;
}

bool BnfParser::parseGrammar()
{
    while (cur().type != Tok::End) {
        if (cur().type == Tok::AtStart) {
            if (!parseDirective()) {
                return false;
            }
        } else if (cur().type == Tok::Ident) {
            if (!parseProduction()) {
                return false;
            }
        } else {
            addError(cur().loc, "意外的记号：期望 @start 指令或产生式");
            return false;
        }
    }

    if (g_.productions.empty()) {
        addError(SourceLocation{1, 1, 0}, "文法为空：没有解析到任何产生式");
        return false;
    }
    return resolveStart();
}

bool BnfParser::parseDirective()
{
    const SourceLocation loc = cur().loc;
    advance(); // 跳过 @start

    if (cur().type != Tok::Ident) {
        addError(cur().loc, "期望非终结符名（@start <name>）");
        return false;
    }
    if (sawStart_) {
        addError(loc, "重复的 @start 指令：至多声明一个开始符号");
        return false;
    }
    sawStart_ = true;
    startName_ = cur().text;
    startLoc_ = cur().loc;
    advance();
    return true;
}

bool BnfParser::parseProduction()
{
    const SourceLocation lhsLoc = cur().loc;
    const std::string lhsName = cur().text;
    advance();

    if (cur().type != Tok::Arrow) {
        addError(cur().loc, "期望 '->'（左部 '" + lhsName + "' 后应有 '->'）");
        return false;
    }
    advance();

    const int lhsId = ensureSymbol(lhsName, SymbolKind::Nonterminal, lhsLoc);
    if (lhsId < 0) {
        return false;
    }
    if (firstLhs_ < 0) {
        firstLhs_ = lhsId;
        firstLhsLoc_ = lhsLoc;
    }

    std::vector<int> rhs;
    bool altHasSymbol = false;      // 当前候选是否已含符号
    bool altExplicitEmpty = false;  // 当前候选是否显式 '#' / 'ε'
    bool firstAlt = true;
    bool done = false;

    while (!done) {
        const Token& tk = cur();
        switch (tk.type) {
        case Tok::Ident:
            // 新产生式左部（上一产生式缺 ';'）
            if (pos_ + 1 < tokens_.size() &&
                tokens_[pos_ + 1].type == Tok::Arrow) {
                addError(tk.loc, "缺少 ';'：上一产生式未以 ';' 结束，此处开始了新产生式");
                return false;
            }
            [[fallthrough]];
        case Tok::Literal:
        case Tok::Dollar: {
            if (altExplicitEmpty) {
                addError(tk.loc, "'#'/ε 只能作为独立候选，不能与其他符号并列");
                return false;
            }
            const int sid = resolveRhsSymbol(tk);
            if (sid < 0) {
                return false;
            }
            rhs.push_back(sid);
            altHasSymbol = true;
            advance();
            break;
        }
        case Tok::Hash:
        case Tok::Epsilon:
            if (altHasSymbol || altExplicitEmpty) {
                addError(tk.loc, "'#'/ε 只能作为独立候选（一个候选至多一个）");
                return false;
            }
            altExplicitEmpty = true;
            advance();
            break;
        case Tok::Pipe:
        case Tok::Semi: {
            if (!firstAlt && !altHasSymbol && !altExplicitEmpty) {
                addError(tk.loc, "空候选必须用 '#' 显式表示");
                return false;
            }
            if (firstAlt && !altHasSymbol && !altExplicitEmpty) {
                addError(tk.loc, "产生式右部为空：空串候选必须显式写 '#'");
                return false;
            }
            addProduction(lhsId, rhs);
            rhs.clear();
            altHasSymbol = false;
            altExplicitEmpty = false;
            firstAlt = false;
            if (tk.type == Tok::Semi) {
                done = true;
            }
            advance();
            break;
        }
        default:
            addError(tk.loc, "期望产生式右部符号、'|' 或 ';'");
            return false;
        }
    }
    return true;
}

int BnfParser::resolveRhsSymbol(const Token& tk)
{
    switch (tk.type) {
    case Tok::Dollar:
        // 内部符号 id 0 = '$'（SymbolKind::EndOfFile，tokenCode 0，DATA_STRUCTURES §5.1）
        return 0;

    case Tok::Ident: {
        // 左部出现过的名字 → 非终结符（两遍扫描判定）
        if (lhsNames_.count(tk.text) != 0) {
            return ensureSymbol(tk.text, SymbolKind::Nonterminal, tk.loc);
        }
        // 否则为终结符：裸名按登记名查 tokenCode（主 A / 备 B 均由调用方提供的 registry 决定）
        const int code = registry_ ? registry_->codeOf(tk.text) : -1;
        if (registry_ && code < 0) {
            addError(tk.loc, "终结符 '" + tk.text +
                             "' 未在词法登记表中定义（.rules / sample.lex 头部）");
            return -1;
        }
        const int sid = ensureSymbol(tk.text, SymbolKind::Terminal, tk.loc);
        if (sid >= 0) {
            g_.symbols[static_cast<size_t>(sid)].tokenCode = code;
        }
        return sid;
    }

    case Tok::Literal: {
        // 引号字面量：按 display 精确匹配；命中后符号名取登记名（如 'if' → KW_IF）
        if (!registry_) {
            return ensureSymbol(tk.text, SymbolKind::Terminal, tk.loc); // tokenCode 保持 -1
        }
        for (const TokenType* t : registry_->orderedTypes()) {
            if (t->display == tk.text) {
                const int sid = ensureSymbol(t->name, SymbolKind::Terminal, tk.loc);
                if (sid >= 0) {
                    g_.symbols[static_cast<size_t>(sid)].tokenCode = t->code;
                }
                return sid;
            }
        }
        addError(tk.loc, "引号字面量 '" + tk.text +
                         "' 与任何单词的 display 均不匹配（.rules / sample.lex 头部）");
        return -1;
    }

    default:
        addError(tk.loc, "内部错误：resolveRhsSymbol 收到非右部符号 token");
        return -1;
    }
}

int BnfParser::ensureSymbol(const std::string& name, SymbolKind kind,
                            const SourceLocation& loc)
{
    for (const Symbol& s : g_.symbols) {
        if (s.name != name) {
            continue;
        }
        if (s.kind == kind) {
            return s.id;
        }
        addError(loc, "符号 '" + name + "' 同时用作终结符与非终结符");
        return -1;
    }

    const int id = static_cast<int>(g_.symbols.size());
    Symbol s;
    s.id = id;
    s.name = name;
    s.kind = kind;
    s.tokenCode = -1;
    g_.symbols.push_back(std::move(s));
    return id;
}

void BnfParser::addProduction(int lhsId, const std::vector<int>& rhs)
{
    Production p;
    p.id = static_cast<int>(g_.productions.size());
    p.lhs = lhsId;
    p.rhs = rhs;
    g_.productions.push_back(std::move(p));
}

bool BnfParser::resolveStart()
{
    if (sawStart_) {
        const int sid = g_.symbolId(startName_, SymbolKind::Nonterminal);
        if (sid < 0) {
            addError(startLoc_, "@start 指向的符号 '" + startName_ +
                                "' 不是非终结符（未在任何产生式左部出现）");
            return false;
        }
        g_.startSymbol = sid;
        return true;
    }

    // 缺省取第一条产生式左部（FILE_FORMATS §3.2），记 Info
    g_.startSymbol = firstLhs_;
    addInfo(firstLhsLoc_, "未声明 @start，缺省取第一条产生式左部为开始符号");
    return true;
}

void BnfParser::addError(const SourceLocation& loc, const std::string& msg)
{
    Diagnostic d;
    d.severity = Severity::Error;
    d.stage = "bnf-parse";
    d.message = msg;
    d.loc = loc;
    diags_.push_back(std::move(d));
    failed_ = true;
}

void BnfParser::addInfo(const SourceLocation& loc, const std::string& msg)
{
    Diagnostic d;
    d.severity = Severity::Info;
    d.stage = "bnf-parse";
    d.message = msg;
    d.loc = loc;
    diags_.push_back(std::move(d));
}

Result<Grammar> BnfParser::makeResult()
{
    Result<Grammar> r;
    r.ok = !failed_;
    if (r.ok) {
        r.value = std::move(g_);
    }
    r.diagnostics = std::move(diags_);
    return r;
}

Result<Grammar> BnfParser::parse(const std::string& text,
                                 const TokenCodeRegistry* registry)
{
    reset();
    registry_ = registry;

    if (!tokenize(text)) {
        return makeResult();
    }

    // 第一遍：收集全部左部名字 = 非终结符集合
    for (size_t i = 0; i + 1 < tokens_.size(); ++i) {
        if (tokens_[i].type == Tok::Ident && tokens_[i + 1].type == Tok::Arrow) {
            lhsNames_.insert(tokens_[i].text);
        }
    }

    // 预置内部符号（DATA_STRUCTURES §5.1）：id 0 = '$'（EndOfFile）、id 1 = '#'（Empty）
    g_.symbols.push_back({0, "$", SymbolKind::EndOfFile, 0});
    g_.symbols.push_back({1, "#", SymbolKind::Empty, -1});

    parseGrammar();
    return makeResult();
}

Result<Grammar> BnfParser::parseFile(const std::string& path,
                                     const TokenCodeRegistry* registry)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        Diagnostic d;
        d.severity = Severity::Error;
        d.stage = "bnf-parse";
        d.message = "无法打开 .bnf 文件：" + path;
        d.loc.line = -1;
        d.loc.column = -1;
        d.loc.offset = -1;
        return Result<Grammar>::failure(std::move(d));
    }
    const std::string text((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    return parse(text, registry);
}

} // namespace compiler::grammar
