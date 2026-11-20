#include "Scanner.h"

#include <utility>

namespace compiler::lexer {

namespace {

// ASCII 大写 → 小写（仅 A-Z；其余不变）。返回 int 便于直接作数组下标。
inline int foldAsciiUpper(int c) {
    return (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
}

// 按字节折叠整个字符串（keywordLookup 反查键用，恒大小写不敏感）
std::string foldAsciiLower(const std::string& s) {
    std::string r = s;
    for (char& ch : r) {
        ch = static_cast<char>(foldAsciiUpper(static_cast<unsigned char>(ch)));
    }
    return r;
}

Diagnostic makeScanDiagnostic(Severity sev, const std::string& msg,
                              const SourceLocation& loc) {
    Diagnostic d;
    d.severity = sev;
    d.stage = "scan";
    d.message = msg;
    d.loc = loc;
    return d;
}

SourceLocation noLoc() { return SourceLocation{-1, -1, -1}; }

} // namespace

bool Scanner::configure(const LexerSpec& spec, const Dfa& dfa) {
    configured_ = false;
    spec_ = nullptr;
    startState_ = -1;
    trans_.clear();
    acceptCode_.clear();
    commentCodes_.clear();
    configDiags_.clear();

    // 校验 DFA：非空、startState 合法
    if (dfa.states.empty() || dfa.startState < 0 ||
        static_cast<size_t>(dfa.startState) >= dfa.states.size()) {
        configDiags_.push_back(makeScanDiagnostic(
            Severity::Error,
            "Scanner::configure：DFA 非法（状态表为空或 startState 越界）", noLoc()));
        return false;
    }

    const size_t n = dfa.states.size();
    trans_.assign(n, std::array<int, 256>{});
    for (auto& row : trans_) {
        row.fill(-1);
    }
    acceptCode_.assign(n, -1);

    for (const DfaState& st : dfa.states) {
        if (st.id < 0 || static_cast<size_t>(st.id) >= n) {
            configDiags_.push_back(makeScanDiagnostic(
                Severity::Error,
                "Scanner::configure：DFA 状态 id 越界（id=" + std::to_string(st.id) + "）",
                noLoc()));
            return false;
        }
        for (const auto& entry : st.transitions) {
            const int target = entry.second;
            if (target < 0 || static_cast<size_t>(target) >= n) {
                configDiags_.push_back(makeScanDiagnostic(
                    Severity::Error,
                    "Scanner::configure：DFA 转移目标越界（state=" + std::to_string(st.id) +
                        " → " + std::to_string(target) + "）",
                    noLoc()));
                return false;
            }
            const unsigned char c = static_cast<unsigned char>(entry.first);
            trans_[static_cast<size_t>(st.id)][static_cast<size_t>(c)] = target;
        }
        if (st.isAccept) {
            acceptCode_[static_cast<size_t>(st.id)] = st.tokenCode;
        }
    }

    for (const LexerRule& r : spec.rules) {
        if (r.isComment) {
            commentCodes_.insert(r.code);
        }
    }

    spec_ = &spec;
    startState_ = dfa.startState;
    configured_ = true;
    return true;
}

ScanResult Scanner::scan(const std::string& source, const std::string& sourcePath) {
    ScanResult result;
    (void)sourcePath;   // 当前仅供诊断展示；T1.8 生成扫描器写 sample.lex 时使用

    if (!configured_ || spec_ == nullptr) {
        result.ok = false;
        result.diagnostics = configDiags_;
        if (result.diagnostics.empty()) {
            result.diagnostics.push_back(makeScanDiagnostic(
                Severity::Error, "Scanner 未 configure，无法扫描", noLoc()));
        }
        return result;
    }

    result.registry = spec_->registry;   // 拷贝，结果自包含

    // 位置跟踪：tracked 之前的所有字节已计入 line/column（累计代价 O(n)）
    size_t tracked = 0;
    int line = 1;
    int column = 1;
    auto positionAt = [&](size_t target) -> SourceLocation {
        while (tracked < target && tracked < source.size()) {
            if (source[tracked] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
            ++tracked;
        }
        SourceLocation loc;
        loc.line = line;
        loc.column = column;
        loc.offset = static_cast<int>(target);
        return loc;
    };

    const size_t len = source.size();
    size_t begin = 0;

    while (true) {
        // 0) 文件结束：EOF token
        if (begin >= len) {
            Token t;
            t.code = TokenCodeRegistry::kEofCode;
            t.kind = TokenKind::EndOfFile;
            t.name = "EOF";
            t.lexeme = "$";
            t.loc = positionAt(begin);
            result.tokens.push_back(std::move(t));
            break;
        }

        const unsigned char c0 = static_cast<unsigned char>(source[begin]);

        // 1) 忽略空白（ignoreChars）
        if (spec_->ignoreChars.count(static_cast<char>(c0)) != 0) {
            ++begin;
            continue;
        }

        // 2) 行注释：@comment line，消费到（不含）行尾 '\n'
        {
            bool consumed = false;
            for (const std::string& prefix : spec_->lineComments) {
                if (prefix.empty()) {
                    continue;
                }
                if (source.compare(begin, prefix.size(), prefix) == 0) {
                    size_t p = begin;
                    while (p < len && source[p] != '\n') {
                        ++p;
                    }
                    begin = p;   // 换行交给 ignoreChars
                    consumed = true;
                    break;
                }
            }
            if (consumed) {
                continue;
            }
        }

        // 3) 块注释：@comment block <open> <close>，消费到闭串之后；
        //    未闭合 → 消费到 EOF 并记 Warning（非致命，继续）
        {
            bool consumed = false;
            for (const auto& bc : spec_->blockComments) {
                const std::string& open = bc.first;
                const std::string& close = bc.second;
                if (open.empty() || close.empty()) {
                    continue;
                }
                if (source.compare(begin, open.size(), open) == 0) {
                    const size_t p = begin + open.size();
                    const size_t closePos = source.find(close, p);
                    if (closePos == std::string::npos) {
                        result.diagnostics.push_back(makeScanDiagnostic(
                            Severity::Warning, "未闭合的块注释（缺少闭合定界符）",
                            positionAt(begin)));
                        begin = len;   // 注释吞掉其余全部
                    } else {
                        begin = closePos + close.size();
                    }
                    consumed = true;
                    break;
                }
            }
            if (consumed) {
                continue;
            }
        }

        // 4) DFA 最长匹配
        {
            int state = startState_;
            int lastAccept = -1;
            size_t lastAcceptPos = static_cast<size_t>(-1);
            size_t cur = begin;
            while (cur < len) {
                unsigned char cc = static_cast<unsigned char>(source[cur]);
                // @case-sensitive=false：源程序原文未折叠，查表前先折叠 ASCII 大写
                // （lexeme 保留原文，仅在查表时折叠）
                const int key = spec_->caseSensitive
                                    ? static_cast<int>(cc)
                                    : foldAsciiUpper(static_cast<int>(cc));
                const int next = trans_[static_cast<size_t>(state)][static_cast<size_t>(key)];
                if (next < 0) {
                    break;
                }
                state = next;
                ++cur;
                if (acceptCode_[static_cast<size_t>(state)] >= 0) {
                    lastAccept = state;
                    lastAcceptPos = cur;
                }
            }

            if (lastAccept >= 0) {
                const int code = acceptCode_[static_cast<size_t>(lastAccept)];

                // 4a) Comment 前缀规则：生成 DFA 但输出丢弃
                if (commentCodes_.count(code) != 0) {
                    begin = lastAcceptPos;
                    continue;
                }

                // 4b) 构造 token（lexeme 保留原文）
                Token t;
                t.code = code;
                t.lexeme = source.substr(begin, lastAcceptPos - begin);
                t.loc = positionAt(begin);

                // 4c) 关键字重分类：keywordLookup 恒大小写不敏感（折叠键）
                const auto kwi = spec_->keywordLookup.find(foldAsciiLower(t.lexeme));
                if (kwi != spec_->keywordLookup.end()) {
                    t.code = kwi->second;
                }

                const TokenType* tt = result.registry.typeOf(t.code);
                if (tt != nullptr) {
                    t.kind = tt->kind;
                    t.name = tt->name;
                } else {
                    t.kind = TokenKind::Unknown;
                    t.name.clear();
                }
                result.tokens.push_back(std::move(t));
                begin = lastAcceptPos;
                continue;
            }
        }

        // 5) 非法字符：ERROR token + stage="scan" 诊断后继续（多错误收集）
        Token t;
        t.code = TokenCodeRegistry::kErrorCode;
        t.kind = TokenKind::Error;
        t.name = "ERROR";
        t.lexeme = source.substr(begin, 1);
        t.loc = positionAt(begin);
        result.tokens.push_back(std::move(t));
        result.diagnostics.push_back(makeScanDiagnostic(
            Severity::Error, "非法字符：'" + t.lexeme + "'（无任何规则可匹配）", t.loc));
        ++begin;
    }

    return result;
}

} // namespace compiler::lexer
