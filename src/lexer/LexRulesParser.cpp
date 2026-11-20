#include "LexRulesParser.h"

#include "RegexParser.h"
#include "RegexAst.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace compiler::lexer {

namespace {

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// 首词（空白分隔），可带回已消费位置
std::string firstWord(const std::string& s, size_t* consumed = nullptr) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    size_t e = b;
    while (e < s.size() && !std::isspace(static_cast<unsigned char>(s[e]))) ++e;
    if (consumed) *consumed = e;
    return s.substr(b, e - b);
}

char toLowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? char(c + 32) : c; }
std::string lowerAscii(std::string s) { for (char& c : s) c = toLowerAscii(c); return s; }

// 简单反转义：\X → X（L 项 / 字面量通用）
std::string unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) { out.push_back(s[i + 1]); ++i; }
        else out.push_back(s[i]);
    }
    return out;
}

// 引号字面量 → 正则模式：对元字符与空白加 '\' 转义，保证 parseRegex 按"逐字面量"解析
std::string literalToRegex(const std::string& lit) {
    std::string out;
    for (char c : lit) {
        if (c == '|' || c == '*' || c == '+' || c == '?' || c == '(' || c == ')' ||
            c == '[' || c == ']' || c == '.' || c == '\\' || c == '~' || c == '#' ||
            c == '-' || c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

// 引号字面量：输入以 '"' 开头；仅 \" 与 \\ 转义，其余原义（含空白）；右引号后只能有空白。
bool parseQuotedLiteral(const std::string& s, std::string& out) {
    if (s.empty() || s[0] != '"') return false;
    out.clear();
    size_t i = 1;
    bool closed = false;
    while (i < s.size()) {
        char c = s[i];
        if (c == '"') { closed = true; ++i; break; }
        if (c == '\\' && i + 1 < s.size()) {
            char n = s[i + 1];
            if (n == '"' || n == '\\') { out.push_back(n); i += 2; continue; }
        }
        out.push_back(c);
        ++i;
    }
    return closed && trim(s.substr(i)).empty();
}

// 字符类 [...] → set<char>（支持区间与 \n \t \r 转义）。失败返回 false
bool parseCharClass(const std::string& s, std::set<char>& out) {
    std::string t = trim(s);
    if (t.size() < 2 || t.front() != '[' || t.back() != ']') return false;
    size_t i = 1;
    const size_t n = t.size() - 1;
    while (i < n) {
        char lo = t[i];
        if (lo == '\\' && i + 1 < n) {
            ++i;
            lo = (t[i] == 'n') ? '\n' : (t[i] == 't') ? '\t' : (t[i] == 'r') ? '\r' : t[i];
            ++i;
        } else {
            ++i;
        }
        if (i < n && t[i] == '-' && i + 1 < n) {
            ++i;
            char hi = t[i];
            if (hi == '\\' && i + 1 < n) {
                ++i;
                hi = (t[i] == 'n') ? '\n' : (t[i] == 't') ? '\t' : (t[i] == 'r') ? '\r' : t[i];
                ++i;
            } else {
                ++i;
            }
            if (lo > hi) return false;
            for (char c = lo;; ++c) { out.insert(c); if (c == hi) break; }
        } else {
            out.insert(lo);
        }
    }
    return true;
}

// 按分隔符切分（空项由调用方校验）
std::vector<std::string> splitOn(char sep, const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        size_t p = s.find(sep, start);
        if (p == std::string::npos) { out.push_back(s.substr(start)); break; }
        out.push_back(s.substr(start, p - start));
        start = p + 1;
    }
    return out;
}

// AST 大小写折叠（ASCII 小写；@case-sensitive false 时调用）。
// Dot 不动；Char/CharClass/NegatedClass 折叠为小写（补集在折叠后字母表上成立）。
void foldCaseAst(regex::RegexNode& n) {
    switch (n.kind) {
        case regex::RegexNodeKind::Char:
            n.ch = toLowerAscii(n.ch);
            break;
        case regex::RegexNodeKind::CharClass:
        case regex::RegexNodeKind::NegatedClass: {
            std::string s;
            s.reserve(n.charClass.size());
            for (char c : n.charClass) s.push_back(toLowerAscii(c));
            n.charClass = std::move(s);
            break;
        }
        default:
            break;
    }
    for (auto& ch : n.children) foldCaseAst(*ch);
}

// 登记名 → 单词类别（ASCII 前缀，兼容 TINY 与 Mini-C 命名）
TokenKind inferKind(const std::string& name, const std::string& display) {
    std::string n = lowerAscii(name);
    if (n == "eof" || n == "error") return n == "eof" ? TokenKind::EndOfFile : TokenKind::Error;
    if (n.rfind("kw", 0) == 0 || n.rfind("keyword", 0) == 0) return TokenKind::Keyword;
    if (n.rfind("id", 0) == 0) return TokenKind::Identifier;
    if (n.rfind("num", 0) == 0) return TokenKind::Number;
    if (n.rfind("str", 0) == 0) return TokenKind::String;
    if (n.rfind("punct", 0) == 0) return TokenKind::Punct;
    if (n.rfind("sym", 0) == 0 || n.rfind("op", 0) == 0 || n.rfind("symbol", 0) == 0) {
        static const std::string puncts = "();,[]{}";
        if (display.size() == 1 && puncts.find(display) != std::string::npos)
            return TokenKind::Punct;
        return TokenKind::Operator;
    }
    return TokenKind::Unknown;
}

} // namespace

void LexRulesParser::reset() {
    spec_ = LexerSpec{};
    diags_.clear();
    failed_ = false;
    ruleIndex_ = 0;
    names_ = regex::RegexNameTable{};      // 内置 digit/letter
    names_.add("eoln", "\n");
    names_.add("eol", "\n");
}

Result<LexerSpec> LexRulesParser::parse(const std::string& text) {
    reset();
    int lineNo = 1;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        std::string line = (end == std::string::npos) ? text.substr(start)
                                                      : text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        handleLine(line, lineNo);
        if (end == std::string::npos) break;
        start = end + 1;
        ++lineNo;
    }
    Result<LexerSpec> r;
    if (failed_) { r.diagnostics = std::move(diags_); return r; }
    r.ok = true;
    r.value = std::move(spec_);
    return r;
}

Result<LexerSpec> LexRulesParser::parseFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        Result<LexerSpec> r;
        Diagnostic d;
        d.severity = Severity::Error;
        d.stage = "rules-parse";
        d.message = "无法打开规则文件: " + path;
        d.loc.line = -1;
        r.diagnostics.push_back(std::move(d));
        return r;
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (content.size() >= 3 && static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF) {
        content.erase(0, 3);   // 去 BOM
    }
    return parse(content);
}

bool LexRulesParser::handleLine(const std::string& line, int lineNo) {
    const std::string s = trim(line);
    if (s.empty()) return true;
    if (s[0] == '#') return true;                    // 注释行
    if (s[0] == '@') return handleDirective(s.substr(1), lineNo);
    return handleRule(s, lineNo);
}

bool LexRulesParser::handleDirective(const std::string& rest, int lineNo) {
    size_t consumed = 0;
    const std::string key = firstWord(rest, &consumed);
    const std::string value = trim(rest.substr(consumed));

    if (key == "name") {
        if (value.empty()) { addError(lineNo, 1, "@name 缺少取值"); return false; }
        spec_.name = value;
        return true;
    }
    if (key == "case-sensitive") {
        if (value == "true") spec_.caseSensitive = true;
        else if (value == "false") spec_.caseSensitive = false;
        else {
            addError(lineNo, 1, "@case-sensitive 需为 true 或 false，实际: '" + value + "'");
            return false;
        }
        return true;
    }
    if (key == "ignore") {
        std::set<char> set;
        if (!parseCharClass(value, set)) {
            addError(lineNo, 1, "@ignore 需为字符类 [...]，实际: '" + value + "'");
            return false;
        }
        spec_.ignoreChars = std::move(set);
        return true;
    }
    if (key == "comment") {
        size_t c = 0;
        const std::string sub = firstWord(value, &c);
        const std::string val = trim(value.substr(c));
        if (sub == "block") {
            const std::string open = firstWord(val);
            size_t c2 = 0;
            firstWord(val, &c2);
            const std::string close = firstWord(trim(val.substr(c2)));
            if (open.empty() || close.empty()) {
                addError(lineNo, 1, "@comment block 需 <open> <close>");
                return false;
            }
            spec_.blockComments.emplace_back(open, close);
            return true;
        }
        if (sub == "line") {
            if (val.empty()) {
                addError(lineNo, 1, "@comment line 缺少前缀");
                return false;
            }
            spec_.lineComments.push_back(val);
            return true;
        }
        addError(lineNo, 1, "@comment 需为 block 或 line，实际: '" + sub + "'");
        return false;
    }
    addError(lineNo, 1, "未知指令 @" + key);
    return false;
}

bool LexRulesParser::handleRule(const std::string& line, int lineNo) {
    const size_t eq = line.find('=');
    if (eq == std::string::npos) { addError(lineNo, 1, "规则行缺少 '='"); return false; }
    std::string left = trim(line.substr(0, eq));
    std::string rhs = trim(line.substr(eq + 1));
    if (left.empty() || rhs.empty()) { addError(lineNo, 1, "规则名或模式为空"); return false; }

    // 可选 "rule" 前缀（TINY 风格；规则名恰为 "rule" 时视为名称本身）
    if (left != "rule" && left.size() > 4 && left.compare(0, 4, "rule") == 0 &&
        std::isspace(static_cast<unsigned char>(left[4]))) {
        left = trim(left.substr(4));
    }

    // 解析 [Name][(_|)<code>][L]：L 仅当其前字符为数字时视为列表标记
    std::string token = left;
    bool isList = false;
    if (!token.empty() && token.back() == 'L' && token.size() >= 2 &&
        std::isdigit(static_cast<unsigned char>(token[token.size() - 2]))) {
        isList = true;
        token.pop_back();
    }
    std::string name;
    int code = -1;
    const size_t lastUnder = token.rfind('_');
    if (lastUnder != std::string::npos) {
        const std::string suffix = token.substr(lastUnder + 1);
        const bool allDigit = !suffix.empty() &&
            std::all_of(suffix.begin(), suffix.end(),
                        [](unsigned char c) { return std::isdigit(c) != 0; });
        if (allDigit) { name = token.substr(0, lastUnder); code = std::stoi(suffix); }
        else { name = token; }
    } else {
        size_t d = token.size();
        while (d > 0 && std::isdigit(static_cast<unsigned char>(token[d - 1]))) --d;
        if (d > 0 && d < token.size()) { name = token.substr(0, d); code = std::stoi(token.substr(d)); }
        else { name = token; }
    }
    if (name.empty()) { addError(lineNo, 1, "规则名非法: '" + left + "'"); return false; }

    const std::string nl = lowerAscii(name);
    if (nl == "eof" || nl == "error") {
        addError(lineNo, 1, "EOF/ERROR 为保留名，不可作规则名");
        return false;
    }

    // 命名类定义行：名字已在命名类表 + 无编码标记 + RHS 为字符类 → 注册/重定义类，不生成 token
    if (code < 0 && !isList && names_.charsOf(name) != nullptr) {
        std::set<char> set;
        if (rhs.size() >= 2 && rhs.front() == '[' && rhs.back() == ']' &&
            parseCharClass(rhs, set)) {
            std::string chars(set.begin(), set.end());
            names_.add(name, chars);
            return true;   // 不占规则槽位、不登记 token
        }
    }

    const bool isKeyword = name.rfind("Keyword", 0) == 0;
    const bool isComment = name.rfind("Comment", 0) == 0;

    // ---- 关键字（Keyword...L / Keyword_<code>）：只登记编码，不生成 DFA ----
    if (isKeyword) {
        std::vector<std::string> spellings;
        if (isList) {
            for (const std::string& item : splitOn('|', rhs)) {
                spellings.push_back(unescape(trim(item)));
            }
        } else {
            std::string lit;
            if (parseQuotedLiteral(rhs, lit)) spellings.push_back(lit);
            else spellings.push_back(unescape(rhs));
        }
        for (size_t i = 0; i < spellings.size(); ++i) {
            const std::string& sp = spellings[i];
            if (sp.empty()) { addError(lineNo, 1, "关键字拼写为空"); return false; }
            if (spec_.registry.codeOf(sp) != -1) {
                addError(lineNo, 1, "名称重复: '" + sp + "'");
                return false;
            }
            TokenType tt;
            tt.name = sp;
            tt.kind = TokenKind::Keyword;
            tt.display = sp;
            const int itemCode = (code >= 0) ? code + static_cast<int>(i) : -1;
            const int actual = (itemCode >= 0) ? spec_.registry.registerToken(tt, itemCode)
                                               : spec_.registry.registerToken(tt);
            if (actual < 0) {
                addError(lineNo, 1, "关键字登记失败（编码/名称冲突）: '" + sp + "'");
                return false;
            }
            spec_.keywordLookup[lowerAscii(sp)] = actual;

            LexerRule rule;
            rule.name = sp;
            rule.code = actual;
            rule.priority = ruleIndex_;
            rule.kind = TokenKind::Keyword;
            rule.isKeyword = true;
            rule.pattern = sp;                     // regex 保持 nullptr
            spec_.rules.push_back(std::move(rule));
        }
        ++ruleIndex_;
        return true;
    }

    // ---- 非关键字：引号字面量 → 转义正则；否则裸正则 ----
    std::string display = name;
    std::string regexText = rhs;
    {
        std::string lit;
        if (parseQuotedLiteral(rhs, lit)) { regexText = literalToRegex(lit); display = lit; }
    }
    Result<std::unique_ptr<regex::RegexNode>> parsed = regex::parseRegex(regexText, names_);
    if (!parsed.ok) {
        const std::string msg = parsed.diagnostics.empty()
            ? "正则解析失败" : parsed.diagnostics.front().message;
        addError(lineNo, 1, "规则 '" + name + "' 正则解析失败: " + msg);
        return false;
    }
    std::unique_ptr<regex::RegexNode> ast = std::move(parsed.value);
    if (!spec_.caseSensitive) foldCaseAst(*ast);

    // ---- 非关键字 L 列表（Symbol_200L= \+ | - | ...）：每项一个字面量规则，生成 DFA ----
    if (isList) {
        const std::vector<std::string> items = splitOn('|', rhs);
        for (size_t i = 0; i < items.size(); ++i) {
            const std::string itemText = trim(items[i]);
            if (itemText.empty()) { addError(lineNo, 1, "L 列表存在空项"); return false; }
            const std::string lit = unescape(itemText);
            if (spec_.registry.codeOf(lit) != -1) {
                addError(lineNo, 1, "名称重复: '" + lit + "'");
                return false;
            }
            Result<std::unique_ptr<regex::RegexNode>> ip = regex::parseRegex(itemText, names_);
            if (!ip.ok) {
                const std::string m = ip.diagnostics.empty()
                    ? "正则解析失败" : ip.diagnostics.front().message;
                addError(lineNo, 1, "L 项正则解析失败: '" + itemText + "': " + m);
                return false;
            }
            std::unique_ptr<regex::RegexNode> itemAst = std::move(ip.value);
            if (!spec_.caseSensitive) foldCaseAst(*itemAst);

            const int itemCode = (code >= 0) ? code + static_cast<int>(i) : -1;
            TokenType tt;
            tt.name = lit;
            tt.kind = isComment ? TokenKind::Comment : inferKind(name, lit);
            tt.display = lit;
            const int actual = (itemCode >= 0) ? spec_.registry.registerToken(tt, itemCode)
                                               : spec_.registry.registerToken(tt);
            if (actual < 0) {
                addError(lineNo, 1, "L 项登记失败（编码/名称冲突）: '" + lit + "'");
                return false;
            }

            LexerRule rule;
            rule.name = lit;
            rule.code = actual;
            rule.priority = ruleIndex_;
            rule.kind = tt.kind;
            rule.isComment = isComment;
            rule.pattern = itemText;
            rule.regex = std::move(itemAst);
            spec_.rules.push_back(std::move(rule));
        }
        ++ruleIndex_;
        return true;
    }

    // ---- 普通单规则（含 Comment_102 注释规则）----
    if (spec_.registry.codeOf(name) != -1) {
        addError(lineNo, 1, "名称重复: '" + name + "'");
        return false;
    }
    TokenType tt;
    tt.name = name;
    tt.kind = isComment ? TokenKind::Comment : inferKind(name, display);
    tt.display = display;
    const int actual = (code >= 0) ? spec_.registry.registerToken(tt, code)
                                   : spec_.registry.registerToken(tt);
    if (actual < 0) {
        addError(lineNo, 1, "规则登记失败（编码/名称冲突）: '" + name + "'");
        return false;
    }

    LexerRule rule;
    rule.name = name;
    rule.code = actual;
    rule.priority = ruleIndex_;
    rule.kind = tt.kind;
    rule.isComment = isComment;
    rule.pattern = rhs;
    rule.regex = std::move(ast);
    spec_.rules.push_back(std::move(rule));
    ++ruleIndex_;
    return true;
}

void LexRulesParser::addError(int lineNo, int col, const std::string& msg) {
    Diagnostic d;
    d.severity = Severity::Error;
    d.stage = "rules-parse";
    d.message = msg;
    d.loc.line = lineNo;
    d.loc.column = col;
    diags_.push_back(std::move(d));
    failed_ = true;
}

} // namespace compiler::lexer