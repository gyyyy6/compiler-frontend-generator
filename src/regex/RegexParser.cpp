#include "RegexParser.h"

#include <utility>

namespace compiler::regex {

namespace {

// 字符区间展开（含端点）
std::vector<int> rangeChars(int lo, int hi) {
    std::vector<int> out;
    out.reserve(static_cast<size_t>(hi - lo + 1));
    for (int c = lo; c <= hi; ++c) {
        out.push_back(c);
    }
    return out;
}

// \w 的字符集合
std::vector<int> wordChars() {
    std::vector<int> v = rangeChars('a', 'z');
    for (int c = 'A'; c <= 'Z'; ++c) v.push_back(c);
    for (int c = '0'; c <= '9'; ++c) v.push_back(c);
    v.push_back('_');
    return v;
}

// \s 的字符集合
std::vector<int> spaceChars() {
    return { ' ', '\t', '\r', '\n' };
}

// 追加单个字符，自动去重
void addClassChar(std::string& out, int c) {
    const char cc = static_cast<char>(c);
    if (out.find(cc) == std::string::npos) {
        out.push_back(cc);
    }
}

// int 字符码集合 → 去重后的 std::string
std::string toCharString(const std::vector<int>& chars) {
    std::string out;
    for (int c : chars) {
        addClassChar(out, c);
    }
    return out;
}

bool isNameStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool isNameChar(char c) {
    return isNameStart(c) || (c >= '0' && c <= '9') || c == '_';
}

} // namespace

// ---- RegexNameTable ----

RegexNameTable::RegexNameTable() {
    classes_["digit"]  = "0123456789";
    classes_["letter"] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
}

void RegexNameTable::add(const std::string& name, std::string chars) {
    classes_[name] = std::move(chars);
}

bool RegexNameTable::contains(const std::string& name) const {
    return classes_.count(name) != 0;
}

const std::string* RegexNameTable::charsOf(const std::string& name) const {
    const auto it = classes_.find(name);
    return it == classes_.end() ? nullptr : &it->second;
}

namespace {

// 递归下降解析器（失败即停止，只记录第一个错误）
class RegexParser {
public:
    RegexParser(const std::string& pattern, const RegexNameTable& names)
        : pattern_(pattern), names_(names) {}

    Result<std::unique_ptr<RegexNode>> parse() {
        std::unique_ptr<RegexNode> root = parseAlternation();
        if (!failed_ && pos_ != pattern_.size()) {
            error(pos_, "无法解析的字符");
            root.reset();
        }
        Result<std::unique_ptr<RegexNode>> result;
        if (failed_) {
            result.diagnostics = std::move(diagnostics_);
        } else {
            result.ok = true;
            result.value = std::move(root);
        }
        return result;
    }

private:
    // ---- 语法子程序 ----

    // alternation := concat ('|' concat)*
    std::unique_ptr<RegexNode> parseAlternation() {
        std::vector<std::unique_ptr<RegexNode>> alts;
        alts.push_back(parseConcat());
        while (!failed_) {
            skipSpace();
            if (peek() != '|') {
                break;
            }
            advance();
            alts.push_back(parseConcat());
        }
        if (alts.size() == 1) {
            return std::move(alts[0]);
        }
        return makeRegexAlternate(std::move(alts));
    }

    // concat := repetition*
    std::unique_ptr<RegexNode> parseConcat() {
        std::vector<std::unique_ptr<RegexNode>> parts;
        while (!failed_) {
            skipSpace();
            if (atEnd() || peek() == '|' || peek() == ')') {
                break;
            }
            parts.push_back(parseRepetition());
        }
        if (parts.empty()) {
            return makeRegexEmpty();
        }
        if (parts.size() == 1) {
            return std::move(parts[0]);
        }
        return makeRegexConcat(std::move(parts));
    }

    // repetition := atom ('*' | '+' | '?')?
    std::unique_ptr<RegexNode> parseRepetition() {
        std::unique_ptr<RegexNode> atom = parseAtom();
        if (failed_) {
            return nullptr;
        }
        const char op = peek();
        if (op == '*' || op == '+' || op == '?') {
            advance();
            const char next = peek();
            if (next == '*' || next == '+' || next == '?') {
                error(pos_, "连续的后缀运算符（* + ? 不能连用）");
                return nullptr;
            }
            const RegexNodeKind kind = op == '*' ? RegexNodeKind::Star
                                     : op == '+' ? RegexNodeKind::Plus
                                                 : RegexNodeKind::Optional;
            return makeRegexUnary(kind, std::move(atom));
        }
        return atom;
    }

    // atom := '#' | '.' | char | class | '(' regex ')' | '~' atom | name
    std::unique_ptr<RegexNode> parseAtom() {
        if (atEnd()) {
            error(pos_, "期望一个原子（字符/类/分组/命名类）");
            return nullptr;
        }
        const char c = peek();
        switch (c) {
            case '#':
                advance();
                return makeRegexEmpty();
            case '.':
                advance();
                return makeRegexDot();
            case '(': {
                advance();
                std::unique_ptr<RegexNode> inner = parseAlternation();
                if (failed_) {
                    return nullptr;
                }
                if (atEnd() || peek() != ')') {
                    error(pos_, "缺少右括号 ')'");
                    return nullptr;
                }
                advance();   // 分组只改变作用域，不产生节点
                return inner;
            }
            case ')':
                error(pos_, "多余的右括号 ')'");
                return nullptr;
            case '|':
                error(pos_, "此处不允许 '|'");
                return nullptr;
            case '*':
            case '+':
            case '?':
                error(pos_, "缺少可重复的原子（* + ? 前必须有原子）");
                return nullptr;
            case '~': {
                const size_t tildePos = pos_;
                advance();
                skipSpace();
                std::unique_ptr<RegexNode> operand = parseAtom();
                if (failed_) {
                    return nullptr;
                }
                if (!isCharLevelAtom(*operand)) {
                    error(tildePos, "'~' 的操作数必须是字符级原子（字符/类/命名类/反义类）");
                    return nullptr;
                }
                return makeRegexUnary(RegexNodeKind::Negate, std::move(operand));
            }
            case '[':
                return parseCharClass();
            case '\\':
                return parseEscape();
            default:
                if (isNameStart(c)) {
                    return parseNamedClass();
                }
                advance();
                return makeRegexChar(c);
        }
    }

    // class := '[' '^'? (char | range | escape)* ']'
    std::unique_ptr<RegexNode> parseCharClass() {
        advance();   // '['
        bool negated = false;
        if (peek() == '^') {
            negated = true;
            advance();
        }
        std::string chars;
        while (!failed_ && !atEnd() && peek() != ']') {
            const size_t elemPos = pos_;
            const std::vector<int> lo = parseClassElement();
            if (failed_) {
                return nullptr;
            }
            // 区间：lo 为单字符，后面紧跟 '-' 且 '-' 后不是 ']'/结尾
            if (lo.size() == 1 && peek() == '-' &&
                pos_ + 1 < pattern_.size() && pattern_[pos_ + 1] != ']') {
                advance();   // '-'
                const std::vector<int> hi = parseClassElement();
                if (failed_) {
                    return nullptr;
                }
                if (hi.size() != 1) {
                    error(elemPos, "区间上界必须是单个字符");
                    return nullptr;
                }
                if (hi[0] < lo[0]) {
                    error(elemPos, "区间下界大于上界");
                    return nullptr;
                }
                for (int c = lo[0]; c <= hi[0]; ++c) {
                    addClassChar(chars, c);
                }
            } else {
                for (int v : lo) {
                    addClassChar(chars, v);
                }
            }
        }
        if (failed_) {
            return nullptr;
        }
        if (atEnd()) {
            error(pos_, "字符类缺少 ']'");
            return nullptr;
        }
        advance();   // ']'
        if (negated) {
            return makeRegexNegatedClass(std::move(chars));
        }
        return makeRegexClass(std::move(chars));
    }

    // 类元素：字面量或转义（\d \w \s 展开为多字符集合）
    std::vector<int> parseClassElement() {
        if (atEnd()) {
            error(pos_, "字符类未闭合");
            return {};
        }
        if (peek() != '\\') {
            const int v = static_cast<unsigned char>(peek());
            advance();
            return { v };
        }
        const size_t escPos = pos_;
        advance();   // '\'
        if (atEnd()) {
            error(escPos, "转义符后缺少字符");
            return {};
        }
        const char e = peek();
        advance();
        bool complement = false;
        const std::vector<int> chars = escapeChars(e, complement);
        if (complement) {
            error(escPos, "字符类内不支持反义转义（\\D \\W \\S）");
            return {};
        }
        return chars;
    }

    // 类外的转义：\n \t \r \\ \d \w \s \D \W \S 及任意字面量转义
    std::unique_ptr<RegexNode> parseEscape() {
        const size_t escPos = pos_;
        advance();   // '\'
        if (atEnd()) {
            error(escPos, "转义符后缺少字符");
            return nullptr;
        }
        const char e = peek();
        advance();
        bool complement = false;
        const std::vector<int> chars = escapeChars(e, complement);
        const std::string classChars = toCharString(chars);
        if (complement) {
            return makeRegexNegatedClass(classChars);
        }
        if (chars.size() == 1) {
            return makeRegexChar(static_cast<char>(chars[0]));
        }
        return makeRegexClass(classChars);
    }

    // 命名类宏替换：名字 → CharClass（字符集合）
    std::unique_ptr<RegexNode> parseNamedClass() {
        const size_t start = pos_;
        while (!atEnd() && isNameChar(peek())) {
            advance();
        }
        const std::string name = pattern_.substr(start, pos_ - start);
        const std::string* chars = names_.charsOf(name);
        if (chars != nullptr) {
            return makeRegexClass(*chars);
        }
        // 未知标识符：按字面字符处理，仅消费首个字符（其余交给 concat 循环），
        // 保证 a、ab、a* 等字面量语义正确；已知命名类（如 digit）不受影响。
        pos_ = start + 1;
        return makeRegexChar(pattern_[start]);
    }

    // ---- 工具 ----

    static bool isCharLevelAtom(const RegexNode& n) {
        switch (n.kind) {
            case RegexNodeKind::Char:
            case RegexNodeKind::CharClass:
            case RegexNodeKind::NegatedClass:
            case RegexNodeKind::Dot:
                return true;
            case RegexNodeKind::Negate:
                return !n.children.empty() && isCharLevelAtom(*n.children[0]);
            default:
                return false;
        }
    }

    // 转义字符 → 字符集合；\D \W \S 置 complement=true
    static std::vector<int> escapeChars(char e, bool& complement) {
        complement = false;
        switch (e) {
            case 'n':  return { 10 };
            case 't':  return { 9 };
            case 'r':  return { 13 };
            case 'd':  return rangeChars('0', '9');
            case 'w':  return wordChars();
            case 's':  return spaceChars();
            case 'D':  complement = true; return rangeChars('0', '9');
            case 'W':  complement = true; return wordChars();
            case 'S':  complement = true; return spaceChars();
            default:   return { static_cast<unsigned char>(e) };
        }
    }

    void error(size_t pos, const std::string& message) {
        if (failed_) {
            return;   // 只记录第一个错误
        }
        failed_ = true;
        Diagnostic d;
        d.severity = Severity::Error;
        d.stage = "regex-parse";
        d.message = message;
        d.loc.line = 1;
        d.loc.column = static_cast<int>(pos) + 1;   // 1 基
        d.loc.offset = static_cast<int>(pos);       // 0 基
        diagnostics_.push_back(std::move(d));
    }

    bool atEnd() const { return pos_ >= pattern_.size(); }
    char peek() const { return atEnd() ? '\0' : pattern_[pos_]; }
    void advance() { if (!atEnd()) ++pos_; }

    // 跳过字符类外的空白（空格/Tab/CR/LF），作为原子分隔符
    void skipSpace() {
        while (!atEnd()) {
            const char c = pattern_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    const std::string& pattern_;
    const RegexNameTable& names_;
    size_t pos_ = 0;
    bool failed_ = false;
    std::vector<Diagnostic> diagnostics_;
};

} // namespace

// ---- 公开 API ----

Result<std::unique_ptr<RegexNode>> parseRegex(
    const std::string& pattern,
    const RegexNameTable& names) {
    return RegexParser(pattern, names).parse();
}

} // namespace compiler::regex
