// T2.7 单测：SampleLexReader（sample.lex 读取 + 与 SampleLexWriter 往返 + T1→T2 链路）。
// 零 Qt、纯 STL；沿用 T1.x 冒烟约定：g++ 独立编译运行，退出码 0 = 全部通过。
// 用法：sample_lex_reader_test <languages 根目录> [真实 sample.lex 路径...]
//
// 覆盖：
//   往返：writer→reader 逐元素一致（混合 kind；CodeOnly 回填 display；注释/EOF 丢弃）
//   真实链路：tiny/minic 的 .rules + samples → 进程内扫描 → writer → reader 一致
//   真实文件（argv 可选）：读 build/generated 下的 sample.lex 再验
//   边界：空文件 / 只有头部无数据行 / 数据区空行 / 截断（缺 #token-table-end）/
//         缺 #file-format / 未登记编码 / CodeOnly 多余字段 / Quoted 缺字段 /
//         引号未闭合 / ERROR 词素多字符 / CRLF+多行数据区 / \" \\ 转义 / BOM /
//         EOF 编码 0 出现在数据区

#include "LexerBuilder.h"
#include "LexRulesParser.h"
#include "SampleLexReader.h"
#include "SampleLexWriter.h"
#include "Scanner.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

int g_checks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            std::cerr << "FAIL @" << __LINE__ << ": " << #cond << std::endl; \
            return false;                                                    \
        }                                                                    \
    } while (0)

std::string loadFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

// 备 B：重解析 .rules → TokenCodeRegistry（测试夹具与真实链路共用）
bool registryFromRules(const std::string& rulesPath,
                       compiler::TokenCodeRegistry& out)
{
    compiler::lexer::LexRulesParser parser;
    auto r = parser.parseFile(rulesPath);
    if (!r.ok) {
        std::cerr << "rules 解析失败: " << rulesPath << std::endl;
        return false;
    }
    out = r.value.registry;
    return true;
}

// 进程内扫描：rules + 源文件 → (registry, tokens 含 EOF)
bool scanSource(const std::string& rulesPath, const std::string& sourcePath,
                compiler::TokenCodeRegistry& regOut,
                std::vector<compiler::Token>& tokensOut)
{
    compiler::lexer::LexRulesParser rp;
    auto rr = rp.parseFile(rulesPath);
    if (!rr.ok) return false;
    compiler::lexer::LexerBuilder builder;
    auto dfar = builder.buildMinimalDfa(rr.value);
    if (!dfar.ok) return false;
    compiler::lexer::Scanner scanner;
    if (!scanner.configure(rr.value, dfar.value)) return false;
    auto sr = scanner.scan(loadFile(sourcePath), sourcePath);
    if (!sr.ok) return false;
    regOut = sr.registry;
    tokensOut = std::move(sr.tokens);
    return true;
}

bool hasAnyError(const compiler::Result<compiler::lexer::SampleLexData>& r)
{
    for (const auto& d : r.diagnostics) {
        if (d.severity == compiler::Severity::Error) return true;
    }
    return false;
}

bool hasDiag(const compiler::Result<compiler::lexer::SampleLexData>& r,
             compiler::Severity sev, const std::string& needle)
{
    for (const auto& d : r.diagnostics) {
        if (d.severity == sev && d.stage == "sample-lex-read" &&
            d.message.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// 该 kind 在 sample.lex 里是否只写编码（词素不落盘，reader 回填 display）
bool isCodeOnlyMode(compiler::TokenKind kind)
{
    return compiler::lexer::renderModeForKind(kind) ==
           compiler::lexer::LexRenderMode::CodeOnly;
}

// ---------- 往返：writer → reader ----------
bool testRoundTrip(const std::string& langRoot)
{
    compiler::TokenCodeRegistry reg;
    if (!registryFromRules(langRoot + "/tiny/tiny.rules", reg)) return false;

    struct Item {
        int code;
        std::string lexeme;
    };
    const std::vector<Item> items = {
        {103, "if"},      // Keyword
        {104, "then"},    // Keyword
        {101, "x"},       // Identifier
        {100, "123"},     // Number
        {100, "3.14"},    // Number
        {200, ":="},      // Operator
        {212, ";"},       // Punct
        {215, "<>"},      // Operator
        {255, "@"},       // Error
    };

    std::vector<compiler::Token> tokens;
    for (const auto& it : items) {
        compiler::Token t;
        t.code = it.code;
        t.lexeme = it.lexeme;
        const compiler::TokenType* ty = reg.typeOf(it.code);
        CHECK(ty != nullptr);
        t.kind = ty->kind;
        t.name = ty->name;
        tokens.push_back(t);
    }
    // 注释与 EOF：writer 应丢弃（Skip 模式）
    {
        compiler::Token c;
        c.code = 102;
        c.kind = compiler::TokenKind::Comment;
        c.lexeme = "{c}";
        tokens.push_back(c);
        tokens.push_back(compiler::lexer::SampleLexReader::makeEofToken());
    }

    const std::string lexText = compiler::lexer::SampleLexWriter::render(
        reg, "TINY", "src/test.tiny", tokens);

    compiler::lexer::SampleLexReader reader;
    auto r = reader.parse(lexText);
    CHECK(r.ok);
    CHECK(!hasAnyError(r));

    // 头部元数据读回
    CHECK(r.value.languageName == "TINY");
    CHECK(r.value.sourcePath == "src/test.tiny");
    CHECK(r.value.generator == "CompilerCourse (task1)");
    CHECK(r.value.version == 1);

    // 期望 = items（注释/EOF 被 writer 丢弃）
    CHECK(r.value.tokens.size() == items.size());
    for (size_t k = 0; k < items.size() && k < r.value.tokens.size(); ++k) {
        CHECK(r.value.tokens[k].code == items[k].code);
        CHECK(r.value.tokens[k].kind == reg.typeOf(items[k].code)->kind);
        CHECK(r.value.tokens[k].name == reg.typeOf(items[k].code)->name);
        if (isCodeOnlyMode(r.value.tokens[k].kind)) {
            // CodeOnly 词素回填 display
            CHECK(r.value.tokens[k].lexeme == reg.typeOf(items[k].code)->display);
        } else {
            CHECK(r.value.tokens[k].lexeme == items[k].lexeme);
        }
        // loc.line = 数据区 token 序号（1 基）
        CHECK(r.value.tokens[k].loc.line == static_cast<int>(k + 1));
    }
    return true;
}

// ---------- 真实链路：进程内扫描 → writer → reader ----------
bool testRealChain(const std::string& langRoot)
{
    const std::vector<std::string> langs = {"tiny", "minic"};
    for (const auto& lang : langs) {
        const std::string rules = langRoot + "/" + lang + "/" + lang + ".rules";
        const std::string source = langRoot + "/" + lang + "/samples/test." +
                                   (lang == "tiny" ? "tiny" : "minic");

        compiler::TokenCodeRegistry scanReg;
        std::vector<compiler::Token> scanTokens;   // 含 EOF
        if (!scanSource(rules, source, scanReg, scanTokens)) {
            std::cerr << "进程内扫描失败: " << lang << std::endl;
            return false;
        }

        const std::string lexText = compiler::lexer::SampleLexWriter::render(
            scanReg, "", source, scanTokens);
        compiler::lexer::SampleLexReader reader;
        auto r = reader.parse(lexText);
        CHECK(r.ok);
        CHECK(!hasAnyError(r));

        // 期望 = 扫描 token 剔除 EOF（注释已被扫描器丢弃）
        std::vector<compiler::Token> expected;
        for (const auto& t : scanTokens) {
            if (t.kind != compiler::TokenKind::EndOfFile) expected.push_back(t);
        }
        CHECK(r.value.tokens.size() == expected.size());
        for (size_t k = 0; k < expected.size() && k < r.value.tokens.size(); ++k) {
            const compiler::Token& got = r.value.tokens[k];
            const compiler::Token& exp = expected[k];
            CHECK(got.code == exp.code);
            CHECK(got.kind == exp.kind);
            if (isCodeOnlyMode(exp.kind)) {
                // CodeOnly 词素不落盘，reader 回填 display
                CHECK(got.lexeme == scanReg.typeOf(exp.code)->display);
            } else {
                CHECK(got.lexeme == exp.lexeme);
            }
        }

        // registry 一致性：全部非保留项 name → code
        for (const auto* ty : r.value.registry.orderedTypes()) {
            if (ty->code == 0 || ty->code == 255) continue;
            CHECK(ty->code == scanReg.codeOf(ty->name));
        }
        std::cout << "real-chain " << lang
                  << ": tokens=" << r.value.tokens.size() << std::endl;
    }
    return true;
}

// ---------- 真实文件（argv 可选）----------
bool testRealFiles(const std::vector<std::string>& paths)
{
    compiler::lexer::SampleLexReader reader;
    for (const auto& p : paths) {
        auto r = reader.parseFile(p);
        std::cout << "real-file " << p << ": ok=" << r.ok
                  << " tokens=" << r.value.tokens.size() << std::endl;
        if (!r.ok) {
            std::cerr << "  FAIL: " << p << std::endl;
            return false;
        }
        CHECK(!r.value.tokens.empty());
    }
    return true;
}

// ---------- 边界 / 负例 ----------
bool testEdgeCases(const std::string& langRoot)
{
    compiler::TokenCodeRegistry reg;
    if (!registryFromRules(langRoot + "/tiny/tiny.rules", reg)) return false;
    compiler::lexer::SampleLexReader reader;

    const std::string header =
        compiler::lexer::SampleLexWriter::renderHeader(reg, "TINY", "src.tiny");

    // 1. 空文件
    {
        auto r = reader.parse("");
        CHECK(!r.ok);
        CHECK(hasDiag(r, compiler::Severity::Error, "#token-table-end"));
    }
    // 2. 只有头部、无数据行 → ok=true，tokens 空
    {
        auto r = reader.parse(header);
        CHECK(r.ok);
        CHECK(r.value.tokens.empty());
    }
    // 3. 数据区空行 → 跳过，ok=true，tokens 空
    {
        auto r = reader.parse(header + "\n\n\n");
        CHECK(r.ok);
        CHECK(r.value.tokens.empty());
    }
    // 4. 截断：缺 #token-table-end
    {
        const size_t pos = header.find("#token-table-end");
        CHECK(pos != std::string::npos);
        auto r = reader.parse(header.substr(0, pos));
        CHECK(!r.ok);
        CHECK(hasDiag(r, compiler::Severity::Error, "#token-table-end"));
    }
    // 5. 缺 #file-format
    {
        const size_t eol = header.find('\n');
        CHECK(eol != std::string::npos);
        auto r = reader.parse(header.substr(eol + 1));
        CHECK(!r.ok);
        CHECK(hasDiag(r, compiler::Severity::Error, "file-format"));
    }
    // 6. 未登记编码 999 → ok=false，后续 token 仍恢复
    {
        auto r = reader.parse(header + "999 103\n");
        CHECK(!r.ok);
        CHECK(hasDiag(r, compiler::Severity::Error, "999"));
        CHECK(r.value.tokens.size() == 1);
        CHECK(r.value.tokens[0].code == 103);
    }
    // 7. CodeOnly 多余字段 → Warning，ok 仍 true
    {
        auto r = reader.parse(header + "103 \"if\" 212\n");
        CHECK(r.ok);
        CHECK(hasDiag(r, compiler::Severity::Warning, "多余字段"));
        CHECK(r.value.tokens.size() == 2);
    }
    // 8. Quoted 缺字段 → Error
    {
        auto r = reader.parse(header + "101 103\n");
        CHECK(!r.ok);
        CHECK(hasDiag(r, compiler::Severity::Error, "缺少引号词素"));
    }
    // 9. 引号未闭合 → Error
    {
        auto r = reader.parse(header + "101 \"abc\n");
        CHECK(!r.ok);
        CHECK(hasDiag(r, compiler::Severity::Error, "未闭合"));
    }
    // 10. ERROR 词素多字符 → Warning
    {
        auto r = reader.parse(header + "255 abc\n");
        CHECK(r.ok);
        CHECK(hasDiag(r, compiler::Severity::Warning, "单字符"));
        CHECK(r.value.tokens.size() == 1);
        CHECK(r.value.tokens[0].code == 255);
        CHECK(r.value.tokens[0].lexeme == "abc");
    }
    // 11. CRLF + 多行数据区
    {
        auto r = reader.parse(header + "103\r\n212 100 42\r\n");
        CHECK(r.ok);
        CHECK(r.value.tokens.size() == 3);
        CHECK(r.value.tokens[0].code == 103);
        CHECK(r.value.tokens[2].lexeme == "42");
    }
    // 12. 转义解码：引号转义与反斜杠转义
    {
        auto r = reader.parse(header + "101 \"a\\\"b\" 101 \"c\\\\d\"\n");
        CHECK(r.ok);
        CHECK(r.value.tokens.size() == 2);
        CHECK(r.value.tokens[0].lexeme == "a\"b");
        CHECK(r.value.tokens[1].lexeme == "c\\d");
    }
    // 13. UTF-8 BOM
    {
        const std::string bom("\xEF\xBB\xBF", 3);
        auto r = reader.parse(bom + header + "103\n");
        CHECK(r.ok);
        CHECK(r.value.tokens.size() == 1);
    }
    // 14. 数据区出现 EOF 编码 0 → Error
    {
        auto r = reader.parse(header + "0\n");
        CHECK(!r.ok);
        CHECK(hasDiag(r, compiler::Severity::Error, "EOF 编码 0"));
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    std::string langRoot = "./test/languages";
    std::vector<std::string> realFiles;
    if (argc > 1) {
        langRoot = argv[1];
    }
    for (int i = 2; i < argc; ++i) {
        realFiles.push_back(argv[i]);
    }

    bool ok = true;
    ok &= testRoundTrip(langRoot);
    ok &= testRealChain(langRoot);
    if (!realFiles.empty()) {
        ok &= testRealFiles(realFiles);
    }
    ok &= testEdgeCases(langRoot);

    if (!ok) {
        std::cerr << "FAIL: 存在未通过的 CHECK（已通过 " << g_checks
                  << " 项检查）" << std::endl;
        return 1;
    }
    std::cout << "sample_lex_reader_test OK, checks=" << g_checks << std::endl;
    return 0;
}
