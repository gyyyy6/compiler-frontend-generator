#pragma once

// sample.lex 读取器（T2.7，依据 docs/FILE_FORMATS.md §4，与 SampleLexWriter 对称）
// 纯 C++17 + STL，不依赖 Qt。sample.lex 文本 → Result<SampleLexData>。
//
// 与 SampleLexWriter 的往返契约（渲染单点化的逆过程）：
//   - 头部 #token 表 → TokenCodeRegistry::fromHeader（T1.2 已实现）；
//   - 数据行字段形式按 renderModeForKind(kind) 判定——与 writer **共用同一份
//     函数**（src/lexer/SampleLexWriter.h），保证"writer 写得出的一定读得回"；
//   - EOF：reader **不自动追加**（sample.lex 无 EOF 行，忠实还原文件）；
//     T2.8 LR 驱动需要时用 makeEofToken() 自行追加（决策点 4）。
//   - 失败（ok=false）时 Result.value 仍携带已恢复的部分 token 与 registry
//     （"恢复继续"的产物，供诊断/预览）；T2.8 仅在 ok 时消费。
//
// 失败诊断：stage = "sample-lex-read"。
//   Error（ok=false，尽量恢复继续）：缺少/非法 #file-format、缺少 #token-table-end
//     （文件截断）、fromHeader 失败、头部区出现非注释行、未登记编码、必需词素字段
//     缺失、引号未闭合、数据区出现 EOF 编码 0。
//   Warning（不影响 ok）：#version 非 1、#token-count 与 #token 行数不符、
//     CodeOnly 多余字段、ERROR 词素非单字符、数据区出现 '#' 行。
//
// Token.loc 约定：
//   注意：sample.lex 不含源程序行列号。Token.loc.line 是数据区 token 序号（1 基），
//   不是源程序行号。真实行列号场景走进程内 Scanner，不走 SampleLexReader。
// 同一实例可重复调用 parse/parseFile，每次调用前内部状态自动重置（reset）。

#include <string>
#include <vector>

#include "Result.h"
#include "SampleLexWriter.h"   // LexRenderMode / renderModeForKind（mode 单点，与 writer 共用）
#include "Token.h"
#include "TokenCodeRegistry.h"

namespace compiler::lexer {

// sample.lex 读取结果（T2.7，FILE_FORMATS §4 的逆过程）
struct SampleLexData {
    TokenCodeRegistry registry;        // 从头部 #token 表重建（含 EOF/ERROR 保留项）
    std::string languageName;          // #language（可为空）
    std::string sourcePath;            // #source
    std::string generator;             // #generator
    int version = 0;                   // #version
    std::vector<Token> tokens;         // 数据行 token，不含 EOF（忠实于文件）
};

class SampleLexReader {
public:
    // 解析 sample.lex 文本 → SampleLexData（UTF-8，去 BOM）。
    // 失败：ok=false，diagnostics 含 Error（stage="sample-lex-read"）。
    Result<SampleLexData> parse(const std::string& text);

    // 读取文件后调用 parse；文件不存在/读取失败 → Error（loc.line = -1）。
    Result<SampleLexData> parseFile(const std::string& path);

    // EOF token 构造（T2.8 LR 驱动在入口追加；reader 不自动追加）。
    // loc = {-1,-1,-1}（合成 token，无文件位置；需要数据区序号语义时调用方可覆盖）。
    static Token makeEofToken();

private:
    void reset();
    void parseDataRange(const std::string& text, size_t begin, size_t end,
                        int& line, int& col);
    bool readQuoted(const std::string& text, size_t& i, size_t end,
                    int& col, std::string* out);
    void readRaw(const std::string& text, size_t& i, size_t end,
                 int& col, std::string* out);
    void addError(const SourceLocation& loc, const std::string& msg);
    void addWarning(const SourceLocation& loc, const std::string& msg);
    Result<SampleLexData> makeResult();

    std::vector<Diagnostic> diags_;
    bool failed_ = false;
    SampleLexData data_;               // 在构结果（值语义，可移动）
};

} // namespace compiler::lexer
