#include "ScannerSourceGenerator.h"

#include "SampleLexWriter.h"    // 渲染单点化：header 行 / renderModeForKind
#include "TokenCodeRegistry.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace compiler::codegen {

namespace {

// 默认模板目录解析：编译定义 → 相对 "templates/"
std::string defaultTemplateDir() {
#ifdef COMPILER_CODEGEN_TEMPLATE_DIR
    return std::string(COMPILER_CODEGEN_TEMPLATE_DIR);
#else
    return {};
#endif
}

// 把 s 转义为 C/C++ 字符串字面量正文（不包引号）
std::string cEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char ch : s) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\x%02X",
                                  static_cast<unsigned char>(ch));
                    out += buf;
                } else {
                    out += ch;
                }
        }
    }
    return out;
}

// 整型数组格式化（perLine 个/行，缩进 indent；末项以 "," 结尾、换行交给模板）
std::string formatInts(const std::vector<int>& values, int perLine, const char* indent) {
    std::string out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i % static_cast<size_t>(perLine) == 0) out += indent;
        out += std::to_string(values[i]);
        if (i + 1 == values.size()) {
            out += ",";                    // 末项：尾随逗号，不换行
        } else if ((i + 1) % static_cast<size_t>(perLine) == 0) {
            out += ",\n";
        } else {
            out += ", ";
        }
    }
    return out;
}

// ACC[]：状态 id → 接受编码 / -1
std::vector<int> buildAcc(const Dfa& dfa, size_t stateCount) {
    std::vector<int> acc(stateCount, -1);
    for (const DfaState& st : dfa.states) {
        if (st.id < 0 || static_cast<size_t>(st.id) >= stateCount) continue;
        if (st.isAccept) {
            acc[static_cast<size_t>(st.id)] = st.tokenCode;
        }
    }
    return acc;
}

// IGN[256]：@ignore 集合
std::vector<int> buildIgn(const lexer::LexerSpec& spec) {
    std::vector<int> ign(256, 0);
    for (char c : spec.ignoreChars) {
        ign[static_cast<size_t>(static_cast<unsigned char>(c))] = 1;
    }
    return ign;
}

// TOK_MODE[256]：编码 → 渲染模式（与 SampleLexWriter::renderModeForKind 一致）
std::vector<int> buildTokMode(const lexer::LexerSpec& spec) {
    std::vector<int> mode(256, static_cast<int>(lexer::LexRenderMode::CodeOnly));
    for (const TokenType* tt : spec.registry.orderedTypes()) {
        if (tt->code < 0 || tt->code > 255) continue;
        mode[static_cast<size_t>(tt->code)] =
            static_cast<int>(lexer::renderModeForKind(tt->kind));
    }
    return mode;
}

// COMMENT_CODES[]：Comment 前缀规则编码；空表用哨兵 -1
std::vector<int> buildCommentCodes(const lexer::LexerSpec& spec) {
    std::vector<int> codes;
    for (const lexer::LexerRule& r : spec.rules) {
        if (r.isComment) codes.push_back(r.code);
    }
    if (codes.empty()) codes.push_back(-1);
    return codes;
}

// KEYWORDS[]：{拼写, 编码} 按 strcmp 升序；空表用哨兵 {"", -1}（末项不带换行，换行交模板）
std::string buildKeywords(const lexer::LexerSpec& spec) {
    std::vector<std::pair<std::string, int>> items(spec.keywordLookup.begin(),
                                                   spec.keywordLookup.end());
    std::sort(items.begin(), items.end(),
              [](const std::pair<std::string, int>& a,
                 const std::pair<std::string, int>& b) {
                  return std::strcmp(a.first.c_str(), b.first.c_str()) < 0;
              });
    if (items.empty()) {
        return "    { \"\", -1 },";
    }
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
        out += "    { \"" + cEscape(items[i].first) + "\", " +
               std::to_string(items[i].second) + " }";
        out += (i + 1 == items.size()) ? "," : ",\n";
    }
    return out;
}

// 行注释定界符数组；空表用哨兵 ""（末项不带换行）
std::string buildLineComments(const lexer::LexerSpec& spec) {
    if (spec.lineComments.empty()) return "    \"\",";
    std::string out;
    for (size_t i = 0; i < spec.lineComments.size(); ++i) {
        out += "    \"" + cEscape(spec.lineComments[i]) + "\"";
        out += (i + 1 == spec.lineComments.size()) ? "," : ",\n";
    }
    return out;
}

// 块注释定界符数组；空表用哨兵 {"",""}（末项不带换行）
std::string buildBlockComments(const lexer::LexerSpec& spec) {
    if (spec.blockComments.empty()) return "    { \"\", \"\" },";
    std::string out;
    for (size_t i = 0; i < spec.blockComments.size(); ++i) {
        out += "    { \"" + cEscape(spec.blockComments[i].first) + "\", \"" +
               cEscape(spec.blockComments[i].second) + "\" }";
        out += (i + 1 == spec.blockComments.size()) ? "," : ",\n";
    }
    return out;
}

// 每状态 switch case：区间合并 + 关键字符分支
std::string buildStates(const Dfa& dfa) {
    std::string out;
    for (const DfaState& st : dfa.states) {
        out += "                case " + std::to_string(st.id) + ":\n";
        // 按字节值升序收集 (byte, target)
        std::vector<std::pair<unsigned char, int>> items;
        items.reserve(st.transitions.size());
        for (const auto& e : st.transitions) {
            items.emplace_back(static_cast<unsigned char>(e.first), e.second);
        }
        std::sort(items.begin(), items.end(),
                  [](const std::pair<unsigned char, int>& a,
                     const std::pair<unsigned char, int>& b) {
                      return a.first < b.first;
                  });
        // 合并连续同目标字节为区间
        struct Interval { int lo, hi, target; };
        std::vector<Interval> intervals;
        for (size_t i = 0; i < items.size();) {
            Interval iv{static_cast<int>(items[i].first),
                        static_cast<int>(items[i].first),
                        items[i].second};
            size_t j = i;
            while (j + 1 < items.size() &&
                   static_cast<int>(items[j + 1].first) == iv.hi + 1 &&
                   items[j + 1].second == iv.target) {
                ++j;
                iv.hi = static_cast<int>(items[j].first);
            }
            intervals.push_back(iv);
            i = j + 1;
        }
        if (intervals.empty()) {
            out += "                    next = -1;\n";   // 死状态：无转移
        } else {
            for (size_t k = 0; k < intervals.size(); ++k) {
                const Interval& iv = intervals[k];
                std::string cond;
                if (iv.lo == iv.hi) {
                    cond = "c == " + std::to_string(iv.lo);
                } else if (iv.hi == iv.lo + 1) {
                    cond = "c == " + std::to_string(iv.lo) + " || c == " +
                           std::to_string(iv.hi);
                } else {
                    cond = "c >= " + std::to_string(iv.lo) + " && c <= " +
                           std::to_string(iv.hi);
                }
                out += "                    " +
                       std::string(k == 0 ? "if" : "else if") + " (" + cond +
                       ") { next = " + std::to_string(iv.target) + "; }\n";
            }
        }
        out += "                    break;\n";
    }
    return out;
}

// foldByte 函数体：@case-sensitive=false 折叠，true 恒等
std::string buildFoldBody(bool caseSensitive) {
    if (caseSensitive) {
        return "return c;";
    }
    return "return (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;";
}

// 多行 C 字符串字面量（相邻字面量自动拼接）
std::string asCStringLiteral(const std::string& text) {
    std::string out;
    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t nl = text.find('\n', pos);
        const std::string line = (nl == std::string::npos)
                                     ? text.substr(pos)
                                     : text.substr(pos, nl - pos + 1);
        out += "    \"" + cEscape(line) + "\"\n";
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    return out;
}

void replaceAll(std::string& s, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

} // namespace

ScannerSourceGenerator::ScannerSourceGenerator(std::string templateDir)
    : templateDir_(std::move(templateDir)) {}

Result<std::string> ScannerSourceGenerator::generate(const lexer::LexerSpec& spec,
                                                     const Dfa& dfa) const {
    Result<std::string> r;

    // 模板定位
    const std::string dir = templateDir_.empty() ? defaultTemplateDir() : templateDir_;
    const std::string tplPath = (dir.empty() ? "templates" : dir) + "/scanner_main.cpp.in";
    std::ifstream tplIn(tplPath, std::ios::binary);
    if (!tplIn) {
        Diagnostic d;
        d.severity = Severity::Error;
        d.stage = "codegen";
        d.message = "无法打开代码生成模板: " + tplPath;
        r.diagnostics.push_back(std::move(d));
        return r;
    }
    std::string tpl((std::istreambuf_iterator<char>(tplIn)),
                    std::istreambuf_iterator<char>());
    tplIn.close();

    // 输入校验：DFA 非空、startState 合法、状态 id 合法
    if (dfa.states.empty() || dfa.startState < 0 ||
        static_cast<size_t>(dfa.startState) >= dfa.states.size()) {
        Diagnostic d;
        d.severity = Severity::Error;
        d.stage = "codegen";
        d.message = "ScannerSourceGenerator：DFA 非法（状态表为空或 startState 越界）";
        r.diagnostics.push_back(std::move(d));
        return r;
    }
    for (const DfaState& st : dfa.states) {
        if (st.id < 0 || static_cast<size_t>(st.id) >= dfa.states.size()) {
            Diagnostic d;
            d.severity = Severity::Error;
            d.stage = "codegen";
            d.message = "ScannerSourceGenerator：DFA 状态 id 越界（id=" +
                        std::to_string(st.id) + "）";
            r.diagnostics.push_back(std::move(d));
            return r;
        }
    }

    // 头部/数据（渲染单点化：全部来自 SampleLexWriter + TokenCodeRegistry）
    const std::string hdrPrefix = lexer::SampleLexWriter::fileFormatLine() +
                                  lexer::SampleLexWriter::versionLine() +
                                  lexer::SampleLexWriter::languageLine(spec.name);
    const std::string hdrGenerator = lexer::SampleLexWriter::generatorLine();
    const std::string tokenTable = spec.registry.toHeader();

    replaceAll(tpl, "@@HDR_PREFIX@@", asCStringLiteral(hdrPrefix));
    replaceAll(tpl, "@@HDR_GENERATOR@@", asCStringLiteral(hdrGenerator));
    replaceAll(tpl, "@@TOKEN_TABLE@@", asCStringLiteral(tokenTable));
    replaceAll(tpl, "@@START_STATE@@", std::to_string(dfa.startState));
    replaceAll(tpl, "@@ACC@@", formatInts(buildAcc(dfa, dfa.states.size()), 8, "    "));
    replaceAll(tpl, "@@IGN@@", formatInts(buildIgn(spec), 16, "    "));
    replaceAll(tpl, "@@TOK_MODE@@", formatInts(buildTokMode(spec), 16, "    "));
    replaceAll(tpl, "@@COMMENT_CODES@@",
               formatInts(buildCommentCodes(spec), 8, "    "));
    replaceAll(tpl, "@@KEYWORDS@@", buildKeywords(spec));
    replaceAll(tpl, "@@LINE_COMMENTS@@", buildLineComments(spec));
    replaceAll(tpl, "@@BLOCK_COMMENTS@@", buildBlockComments(spec));
    replaceAll(tpl, "@@STATES@@", buildStates(dfa));
    replaceAll(tpl, "@@FOLD@@", buildFoldBody(spec.caseSensitive));

    // 残留占位符检测（防止半成品）
    if (tpl.find("@@") != std::string::npos) {
        Diagnostic d;
        d.severity = Severity::Error;
        d.stage = "codegen";
        d.message = "代码生成模板存在未替换占位符（@@...）";
        r.diagnostics.push_back(std::move(d));
        return r;
    }

    return Result<std::string>::success(std::move(tpl));
}

} // namespace compiler::codegen
