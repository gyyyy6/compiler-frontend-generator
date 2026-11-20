#pragma once

// 表驱动扫描器（T1.7，定稿方案见 docs/PROGRESS.md「T1.7 表驱动扫描器」）
// 纯 C++17 + STL，不依赖 Qt。
//
// 职责：读源文本 → 按最小 DFA 最长匹配扫描 → 处理空白/注释/关键字重分类/非法字符
//       → 输出 vector<Token>（含末尾 EOF token）。
//
// 关键设计：
//  - configure(spec, minimalDfa)：一次性预处理——trans_[state][256] 密集转移表、
//    acceptCode_[state]（非接受态 -1）、commentCodes_（Comment 前缀规则编码）。
//  - scan(text, path)：可重复调用，每次独立扫描。
//  - 最长匹配：记录"最后一次进入接受态的位置"，死路/EOF 时回退到该位置产出 token。
//  - @case-sensitive=false：T1.6 折叠的是规则 AST（DFA 表只存小写），源程序原文未折叠；
//    扫描查表时先把 source[cur] 的 ASCII 大写折叠为小写再查 trans_，lexeme 保留原文。
//  - 注释两种机制：①@comment line/block 指令——token 起点精确匹配，直接跳过；
//    ②Comment 前缀规则——生成 DFA，命中后经 commentCodes_ 判定并丢弃。
//  - 关键字：匹配后按 keywordLookup（ASCII 小写折叠键）重分类，恒大小写不敏感。
//
// 失败诊断 stage = "scan"；configure 失败返回 false，诊断见 configDiagnostics()。
// 本类不持有/不拷贝 LexerSpec 与 Dfa（只读引用）：调用方必须保证 configure 传入的
// spec 与 dfa 生命周期覆盖所有 scan 调用。

#include <array>
#include <cstddef>
#include <set>
#include <string>
#include <vector>

#include "DFA.h"
#include "Diagnostic.h"
#include "LexerSpec.h"
#include "Token.h"
#include "TokenCodeRegistry.h"

namespace compiler::lexer {

// 一次扫描结果：token 流 + registry 拷贝 + 诊断（任务三 I4 可消费）
struct ScanResult {
    bool ok = true;                        // 仅"未 configure"为 false；扫描期错误为 true（多错误收集）
    std::vector<Token> tokens;             // 含末尾 EOF token
    TokenCodeRegistry registry;            // spec.registry 拷贝，结果自包含
    std::vector<Diagnostic> diagnostics;   // stage="scan"
};

class Scanner {
public:
    // 绑定规格与最小 DFA 并预处理。失败返回 false，诊断见 configDiagnostics()。
    bool configure(const LexerSpec& spec, const Dfa& minimalDfa);

    // 扫描文本 → token 流（含 EOF）。未 configure 时返回 ok=false 的诊断结果。
    ScanResult scan(const std::string& source, const std::string& sourcePath = {});

    // configure 失败/未调用时的诊断（供 GUI/管线展示）
    const std::vector<Diagnostic>& configDiagnostics() const { return configDiags_; }

private:
    bool configured_ = false;
    const LexerSpec* spec_ = nullptr;          // 只读引用，不拷贝
    int  startState_ = -1;                     // 最小 DFA 起点
    std::vector<std::array<int, 256>> trans_;  // 状态 × 字节 → 目标状态；-1 = 无转移
    std::vector<int> acceptCode_;              // 状态 → tokenCode；非接受态 -1
    std::set<int> commentCodes_;               // Comment 前缀规则编码（DFA 命中即丢弃）
    std::vector<Diagnostic> configDiags_;
};

} // namespace compiler::lexer
