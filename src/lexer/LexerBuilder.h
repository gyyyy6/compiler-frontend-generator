#pragma once

// 词法装配层（T1.6/T1.7）：LexerSpec → 总 NFA → DFA → 最小 DFA。
// 纯 C++17 + STL，不依赖 Qt。复用 regex/ 模块：buildNfa / subsetConstruct / minimizeDfa。
//
// 职责（方案二：合并总 DFA）：
//  - buildNfa：遍历 spec.rules，构建总 NFA。关键规则：
//      * 【关键】显式跳过 isKeyword=true 的规则（其 regex=nullptr，只登记编码、
//        不参与 DFA；关键字由 ID 规则识别后经 keywordLookup 重分类）；
//      * 其余规则要求 regex != nullptr，否则返回 stage="nfa-build" 失败诊断
//        （绝不崩溃、绝不静默跳过）；
//      * 每条规则 NFA 以共起点 ε 拼接；接受态打 tokenCode=rule.code、
//        priority=rule.priority 戳记。
//  - 状态 id 重编号：每个独立 NFA 的 state.id / edge.target 均从 0 起，合并时必须
//    重编号——用 offset（当前 merged.states.size()）整体平移后追加（详见 .cpp）。
//  - 空串校验：从总 NFA 起点做 ε-闭包，若闭包含任一接受态 → 某条"生成 DFA 的
//    规则"可接受空串（会造成扫描死循环）→ 返回 stage="nfa-build" 失败。
//    注意：此处只针对有 regex 的规则（keyword 已不在总 NFA 中，不参与判断）。
//  - buildDfa / buildMinimalDfa：依次复用 regex::subsetConstruct / regex::minimizeDfa。
//
// 失败诊断：stage = "nfa-build"（空串/装配错误）、"dfa-build"、"dfa-minimize"。
// 本类为无状态工具：const 引用入参，不持有/不拷贝 LexerSpec。

#include "LexerSpec.h"
#include "NFA.h"
#include "DFA.h"
#include "Result.h"

namespace compiler::lexer {

class LexerBuilder {
public:
    // 总 NFA：共起点 ε 拼接全部可匹配规则（显式跳过 keyword）；
    // 规则缺 AST 或可接受空串时返回失败。
    Result<Nfa> buildNfa(const LexerSpec& spec);

    // 总 DFA：subsetConstruct(buildNfa(spec))。
    Result<Dfa> buildDfa(const LexerSpec& spec);

    // 最小 DFA：minimizeDfa(buildDfa(spec))。
    Result<Dfa> buildMinimalDfa(const LexerSpec& spec);
};

} // namespace compiler::lexer