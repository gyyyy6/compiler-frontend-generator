#include "SampleLexWriter.h"

#include <string>

namespace compiler::lexer {

LexRenderMode renderModeForKind(TokenKind kind) {
    switch (kind) {
        case TokenKind::EndOfFile:  return LexRenderMode::Skip;
        case TokenKind::Keyword:    return LexRenderMode::CodeOnly;
        case TokenKind::Identifier: return LexRenderMode::Quoted;
        case TokenKind::Number:     return LexRenderMode::Raw;
        case TokenKind::String:     return LexRenderMode::Quoted;   // 扩展：按字符串字面量输出
        case TokenKind::Operator:   return LexRenderMode::CodeOnly;
        case TokenKind::Punct:      return LexRenderMode::CodeOnly;
        case TokenKind::Comment:    return LexRenderMode::Skip;
        case TokenKind::Error:      return LexRenderMode::Error;
        case TokenKind::Unknown:    return LexRenderMode::CodeOnly;
    }
    return LexRenderMode::CodeOnly;
}

std::string SampleLexWriter::fileFormatLine() {
    return "#file-format: sample.lex\n";
}

std::string SampleLexWriter::versionLine() {
    return "#version: 1\n";
}

std::string SampleLexWriter::languageLine(const std::string& name) {
    return "#language: " + name + "\n";
}

std::string SampleLexWriter::sourceLine(const std::string& path) {
    return "#source: " + path + "\n";
}

std::string SampleLexWriter::generatorLine() {
    return "#generator: CompilerCourse (task1)\n";
}

std::string SampleLexWriter::renderHeader(const TokenCodeRegistry& registry,
                                          const std::string& languageName,
                                          const std::string& sourcePath) {
    return fileFormatLine() + versionLine() + languageLine(languageName) +
           sourceLine(sourcePath) + generatorLine() + registry.toHeader();
}

void SampleLexWriter::writeToken(std::string& out, const Token& t, bool& first) {
    const LexRenderMode mode = renderModeForKind(t.kind);
    if (mode == LexRenderMode::Skip) {
        return;                       // EOF / 注释：不输出、不推进 first
    }
    if (!first) {
        out += ' ';
    }
    first = false;
    switch (mode) {
        case LexRenderMode::CodeOnly:
            out += std::to_string(t.code);
            break;
        case LexRenderMode::Quoted:
            out += std::to_string(t.code) + " \"" + t.lexeme + '"';
            break;
        case LexRenderMode::Raw:
            out += std::to_string(t.code) + ' ' + t.lexeme;
            break;
        case LexRenderMode::Error:
            out += std::to_string(t.code) + ' ' + t.lexeme;
            break;
        case LexRenderMode::Skip:
            break;
    }
}

std::string SampleLexWriter::render(const TokenCodeRegistry& registry,
                                    const std::string& languageName,
                                    const std::string& sourcePath,
                                    const std::vector<Token>& tokens) {
    std::string out = renderHeader(registry, languageName, sourcePath);
    bool first = true;
    for (const Token& t : tokens) {
        writeToken(out, t, first);
    }
    out += '\n';   // 数据区始终以换行结束（与生成扫描器一致）
    return out;
}

} // namespace compiler::lexer
