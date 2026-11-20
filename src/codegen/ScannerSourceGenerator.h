#pragma once

// T1.8 生成 C++ 扫描器（v2 方案，见 docs/PROGRESS.md「T1.8」）
// 纯 C++17 + STL，不依赖 Qt。
//
// 输入：LexerSpec + 最小 DFA → 输出：独立 C++ 扫描器源码文本（零 Qt、仅 STL）。
// 生成风格（方法二）：state 变量 + while(!done){ switch(state){...} }，每 case 内嵌
//   区间合并 + 关键字符分支，多字符符号在后续 case 续读；循环外统一回溯
//   lastCode>=0 产 token / 否则 ERROR 单字符。
// 内嵌 ACC[]/KEYWORDS[]/IGN[256]/TOK_MODE[256]/注释定界符/COMMENT_CODES[]/START_STATE；
// 不嵌 256 转移表；头部省略 #generated-at（与 SampleLexWriter 逐字节一致）。
// 失败诊断 stage = "codegen"。

#include <string>

#include "DFA.h"
#include "LexerSpec.h"
#include "Result.h"

namespace compiler::codegen {

class ScannerSourceGenerator {
public:
    // templateDir 为空时自动解析模板路径：
    //   ①编译定义 COMPILER_CODEGEN_TEMPLATE_DIR（CMake 注入）→ ②相对 "templates/"。
    explicit ScannerSourceGenerator(std::string templateDir = {});

    // LexerSpec + 最小 DFA → 独立 C++ 扫描器源码。失败诊断 stage="codegen"。
    Result<std::string> generate(const lexer::LexerSpec& spec, const Dfa& minimalDfa) const;

private:
    std::string templateDir_;
};

} // namespace compiler::codegen
