// sample.lex 读取器实现（T2.7，依据 docs/FILE_FORMATS.md §4）
//
// 实现要点：
//   - 头部逐行扫描到 #token-table-end，元数据就地解析，#token 表整体交给
//     TokenCodeRegistry::fromHeader（T1.2）；fromHeader 只认 "#token " 行，
//     其余头部行天然忽略，故传入头部切片即可。
//   - 数据区流式解析：空白为分隔符（CRLF/LF/空行兼容）；字段形式按
//     renderModeForKind(kind) 判定——与 SampleLexWriter 共用同一函数（mode 单点）。
//   - 损坏恢复：结构破坏记 Error 后尽量继续，避免单点损坏丢整份文件；
//     存在任一 Error 即 ok=false（T2.8 仅在 ok 时消费）。

#include "SampleLexReader.h"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <utility>

namespace compiler::lexer {

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }

std::string trim(const std::string& s)
{
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
    return s.substr(b, e - b);
}

int parseInt(const std::string& s) { return std::atoi(s.c_str()); } // 非法输入 → 0

// 头部元数据（仅 .cpp 内部使用）
struct HeaderMeta {
    bool hasFileFormat = false;
    bool fileFormatOk = false;
    bool hasVersion = false;
    int version = 0;
    bool hasTokenCount = false;
    int tokenCount = -1;
    int tokenLineCount = 0;   // #token 行数（不含保留项 0/255）
};

// 解析一行头部元数据；其它未知 '#' 行前向兼容忽略
void parseHeaderLine(const std::string& line, SampleLexData& data, HeaderMeta& m)
{
    if (line.rfind("#file-format:", 0) == 0) {
        m.hasFileFormat = true;
        m.fileFormatOk = (trim(line.substr(13)) == "sample.lex");
    } else if (line.rfind("#version:", 0) == 0) {
        m.hasVersion = true;
        m.version = parseInt(trim(line.substr(9)));
        data.version = m.version;
    } else if (line.rfind("#language:", 0) == 0) {
        data.languageName = trim(line.substr(10));
    } else if (line.rfind("#source:", 0) == 0) {
        data.sourcePath = trim(line.substr(8));
    } else if (line.rfind("#generator:", 0) == 0) {
        data.generator = trim(line.substr(11));
    } else if (line.rfind("#token-count:", 0) == 0) {
        m.hasTokenCount = true;
        m.tokenCount = parseInt(trim(line.substr(13)));
    } else if (line.rfind("#token ", 0) == 0) {
        std::istringstream ls(line.substr(7));
        int code = 0;
        std::string name, display;
        if (ls >> code >> name >> display && code != 0 && code != 255) {
            ++m.tokenLineCount;
        }
    }
}

} // namespace

void SampleLexReader::reset()
{
    diags_.clear();
    failed_ = false;
    data_ = SampleLexData{};
}

Result<SampleLexData> SampleLexReader::parse(const std::string& text)
{
    reset();
    const size_t n = text.size();
    size_t i = 0;
    if (n >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        i = 3;   // 去 UTF-8 BOM
    }

    int line = 1, col = 1;
    const SourceLocation headerStart{line, col, static_cast<int>(i)};

    // ---- 头部：逐行扫描到 #token-table-end ----
    std::string header;   // 头部切片（供 fromHeader；含全部 #token 行）
    HeaderMeta m;
    bool foundEnd = false;
    size_t dataStart = n;
    while (i < n) {
        const SourceLocation lLoc{line, col, static_cast<int>(i)};
        size_t j = i;
        while (j < n && text[j] != '\n') ++j;
        std::string l = text.substr(i, j - i);
        if (!l.empty() && l.back() == '\r') l.pop_back();

        if (l.rfind("#token-table-end", 0) == 0) {
            foundEnd = true;
            dataStart = (j < n) ? j + 1 : n;
            if (j < n) {
                ++line;
                col = 1;
            } else {
                col += static_cast<int>(j - i);
            }
            break;
        }

        if (l.empty()) {
            header += "\n";
        } else if (l[0] == '#') {
            header += l + "\n";
            parseHeaderLine(l, data_, m);
        } else {
            header += l + "\n";
            addError(lLoc, "头部区出现非注释行（可能缺少 #token-table-end）");
        }
        i = (j < n) ? j + 1 : n;
        ++line;
        col = 1;
    }

    if (!foundEnd) {
        addError(headerStart, "文件截断：缺少 #token-table-end");
        return makeResult();
    }

    // ---- 头部校验 ----
    if (!m.hasFileFormat) {
        addError(headerStart, "缺少 #file-format 行");
    } else if (!m.fileFormatOk) {
        addError(headerStart, "#file-format 应为 sample.lex");
    }
    if (m.hasVersion && m.version != 1) {
        addWarning(headerStart, "不支持的 #version：" + std::to_string(m.version));
    }
    if (m.hasTokenCount && m.tokenCount != m.tokenLineCount) {
        addWarning(headerStart, "#token-count 与 #token 行数不符（声明 " +
                                std::to_string(m.tokenCount) + "，实际 " +
                                std::to_string(m.tokenLineCount) + "）");
    }

    // ---- 重建 registry（T1.2 fromHeader）----
    if (!data_.registry.fromHeader(header)) {
        addError(headerStart, "sample.lex 头部 #token 表非法（fromHeader 失败）");
        return makeResult();
    }

    // ---- 数据区 ----
    parseDataRange(text, dataStart, n, line, col);
    return makeResult();
}

void SampleLexReader::parseDataRange(const std::string& text, size_t begin, size_t end,
                                     int& line, int& col)
{
    size_t i = begin;
    int tokenIndex = 0;

    auto skipWs = [&]() {
        while (i < end) {
            const char c = text[i];
            if (c == ' ' || c == '\t' || c == '\r') {
                ++i;
                ++col;
            } else if (c == '\n') {
                ++i;
                ++line;
                col = 1;
            } else {
                break;
            }
        }
    };
    auto locHere = [&]() {
        SourceLocation l;
        l.line = line;
        l.column = col;
        l.offset = static_cast<int>(i);
        return l;
    };

    while (true) {
        skipWs();
        if (i >= end) break;

        // 数据区 '#' 行（不预期，容忍跳过）
        if (text[i] == '#') {
            addWarning(locHere(), "数据区出现 '#' 行（sample.lex 数据区不应有注释），已跳过");
            while (i < end && text[i] != '\n') {
                ++i;
                ++col;
            }
            continue;
        }

        const SourceLocation tokLoc = locHere();

        // 读编码
        if (!isDigit(text[i])) {
            addError(locHere(), "期望 token 编码（数据区含非数字字符）");
            while (i < end && !isSpace(text[i])) {
                ++i;
                ++col;
            }
            continue;
        }
        int code = 0;
        while (i < end && isDigit(text[i])) {
            code = code * 10 + (text[i] - '0');
            ++i;
            ++col;
        }

        // EOF 编码 0 不应出现在数据区（sample.lex 不写 EOF 行）
        if (code == TokenCodeRegistry::kEofCode) {
            addError(tokLoc, "数据区出现 EOF 编码 0（sample.lex 不写 EOF 行）");
            continue;
        }

        const TokenType* type = data_.registry.typeOf(code);
        if (!type) {
            addError(tokLoc, "未登记编码 " + std::to_string(code) +
                             "（数据行与头部 #token 表不一致）");
            // 按语法猜字段并消费，尽量恢复继续
            skipWs();
            if (i < end && text[i] == '"') {
                readQuoted(text, i, end, col, nullptr);
            } else if (i < end && !isDigit(text[i])) {
                readRaw(text, i, end, col, nullptr);
            }
            continue;
        }

        ++tokenIndex;

        // mode 判定与 writer 共用 renderModeForKind（渲染单点化的逆过程）
        const LexRenderMode mode = renderModeForKind(type->kind);
        std::string lexeme;

        switch (mode) {
        case LexRenderMode::CodeOnly: {
            skipWs();
            if (i < end && text[i] == '"') {
                addWarning(locHere(), "CodeOnly token（编码 " + std::to_string(code) +
                                      "）后出现多余字段，已忽略");
                readQuoted(text, i, end, col, nullptr);
            } else if (i < end && !isDigit(text[i])) {
                addWarning(locHere(), "CodeOnly token（编码 " + std::to_string(code) +
                                      "）后出现多余字段，已忽略");
                readRaw(text, i, end, col, nullptr);
            }
            // sample.lex 未存词素：回填 display（GUI 可视化语义）
            lexeme = type->display;
            break;
        }
        case LexRenderMode::Quoted: {
            skipWs();
            if (i >= end || text[i] != '"') {
                addError(tokLoc, "标识符/字符串 token（编码 " + std::to_string(code) +
                                 "）缺少引号词素字段");
                break;
            }
            if (!readQuoted(text, i, end, col, &lexeme)) {
                addError(tokLoc, "引号词素未闭合（编码 " + std::to_string(code) + "）");
            }
            break;
        }
        case LexRenderMode::Raw: {
            skipWs();
            if (i >= end) {
                addError(tokLoc, "数值 token（编码 " + std::to_string(code) +
                                 "）缺少词素字段");
                break;
            }
            readRaw(text, i, end, col, &lexeme);
            break;
        }
        case LexRenderMode::Error: {
            skipWs();
            readRaw(text, i, end, col, &lexeme);
            if (lexeme.size() != 1) {
                addWarning(tokLoc, "ERROR token 词素应为单字符，实际 " +
                                   std::to_string(lexeme.size()) + " 字符");
            }
            break;
        }
        case LexRenderMode::Skip:
            // writer 不会把 EOF/注释写入数据区；防御分支（理论不可达）
            break;
        }

        Token t;
        t.code = code;
        t.kind = type->kind;
        t.name = type->name;
        t.lexeme = lexeme;
        // 注意：sample.lex 不含源程序行列号。line 是数据区 token 序号（1 基），
        // 不是源程序行号。真实行列号场景走进程内 Scanner，不走 SampleLexReader。
        t.loc.line = tokenIndex;
        t.loc.column = 1;
        t.loc.offset = tokLoc.offset;
        data_.tokens.push_back(std::move(t));
    }
}

bool SampleLexReader::readQuoted(const std::string& text, size_t& i, size_t end,
                                 int& col, std::string* out)
{
    if (out) out->clear();
    ++i;   // 跳过开引号
    ++col;
    bool closed = false;
    while (i < end) {
        const char c = text[i];
        if (c == '"') {
            ++i;
            ++col;
            closed = true;
            break;
        }
        if (c == '\n') break;   // 跨行未闭合
        if (c == '\\' && i + 1 < end && (text[i + 1] == '"' || text[i + 1] == '\\')) {
            if (out) out->push_back(text[i + 1]);   // 解码转义：\" → 双引号、\\ → 反斜杠
            i += 2;
            col += 2;
            continue;
        }
        if (out) out->push_back(c);
        ++i;
        ++col;
    }
    return closed;
}

void SampleLexReader::readRaw(const std::string& text, size_t& i, size_t end,
                              int& col, std::string* out)
{
    if (out) out->clear();
    while (i < end && !isSpace(text[i])) {
        if (out) out->push_back(text[i]);
        ++i;
        ++col;
    }
}

void SampleLexReader::addError(const SourceLocation& loc, const std::string& msg)
{
    Diagnostic d;
    d.severity = Severity::Error;
    d.stage = "sample-lex-read";
    d.message = msg;
    d.loc = loc;
    diags_.push_back(std::move(d));
    failed_ = true;
}

void SampleLexReader::addWarning(const SourceLocation& loc, const std::string& msg)
{
    Diagnostic d;
    d.severity = Severity::Warning;
    d.stage = "sample-lex-read";
    d.message = msg;
    d.loc = loc;
    diags_.push_back(std::move(d));
}

Result<SampleLexData> SampleLexReader::makeResult()
{
    Result<SampleLexData> r;
    r.ok = !failed_;
    // 失败时也带回已恢复的部分数据（"恢复继续"的产物，见头注释）
    r.value = std::move(data_);
    r.diagnostics = std::move(diags_);
    return r;
}

Result<SampleLexData> SampleLexReader::parseFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        Diagnostic d;
        d.severity = Severity::Error;
        d.stage = "sample-lex-read";
        d.message = "无法打开 sample.lex 文件：" + path;
        d.loc.line = -1;
        d.loc.column = -1;
        d.loc.offset = -1;
        return Result<SampleLexData>::failure(std::move(d));
    }
    const std::string text((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    return parse(text);
}

Token SampleLexReader::makeEofToken()
{
    Token t;
    t.code = TokenCodeRegistry::kEofCode;
    t.kind = TokenKind::EndOfFile;
    t.name = "EOF";
    t.lexeme = "$";   // 保留编码 0 的 display 约定
    t.loc.line = -1;  // 合成 token，无文件位置
    t.loc.column = -1;
    t.loc.offset = -1;
    return t;
}

} // namespace compiler::lexer
