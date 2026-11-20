#pragma once

// sample.lex 渲染器（T1.8，依据 docs/FILE_FORMATS.md §4）
// 纯 C++17 + STL，不依赖 Qt。
//
// 渲染单点化：本类是宿主侧唯一渲染 sample.lex 的实现（头部 + 数据行），
// 供 --lex 管线 / T1.9 GUI 使用；T1.8 生成的独立扫描器以本类为契约，
// 逐字节一致（e2e 比对校验）。头部省略 #generated-at（T1.8 v2 定稿）。

#include <string>
#include <vector>

#include "Token.h"               // Token / TokenKind
#include "TokenCodeRegistry.h"   // TokenCodeRegistry

namespace compiler::lexer {

// 数据行渲染模式（FILE_FORMATS.md §4.3；与生成扫描器 TOK_MODE 语义一致）
enum class LexRenderMode : int {
    Skip     = 0,   // 注释：不输出
    CodeOnly = 1,   // 关键字/符号/界符：只输出编码
    Quoted   = 2,   // 标识符：编码 "lexeme"
    Raw      = 3,   // 数值：编码 lexeme
    Error    = 4,   // 非法字符：255 <单字符>
};

// 由 TokenKind 决定渲染模式（唯一映射点：SampleLexWriter 与 codegen 共用）
LexRenderMode renderModeForKind(TokenKind kind);

// sample.lex 渲染器（宿主侧唯一实现）
class SampleLexWriter {
public:
    // 头部固定行（codegen 复用，保证与生成扫描器逐字节一致）
    static std::string fileFormatLine();                       // "#file-format: sample.lex\n"
    static std::string versionLine();                          // "#version: 1\n"
    static std::string languageLine(const std::string& name);  // "#language: <name>\n"
    static std::string sourceLine(const std::string& path);    // "#source: <path>\n"
    static std::string generatorLine();                        // "#generator: CompilerCourse (task1)\n"

    // 头部（file-format/version/language/source/generator + token 表；无 #generated-at）
    static std::string renderHeader(const TokenCodeRegistry& registry,
                                    const std::string& languageName,
                                    const std::string& sourcePath);

    // 追加一条 token 的数据行（EOF/注释不输出、不推进 first）
    static void writeToken(std::string& out, const Token& t, bool& first);

    // 整体渲染：header + 全部 token 数据行（空格分隔）+ 结尾换行
    static std::string render(const TokenCodeRegistry& registry,
                              const std::string& languageName,
                              const std::string& sourcePath,
                              const std::vector<Token>& tokens);
};

} // namespace compiler::lexer
