#pragma once

// LR 驱动 + ParseTree 构建（T2.8，依据 docs/PROGRESS.md 决策点 4 / 决策点 5 B 与 docs/DATA_STRUCTURES.md §5.6）
// 纯 C++17 + STL，不依赖 Qt。
// 输入：化简后 Grammar + ParseTableData（T2.6 产出）+ token 流 → Result<ParseTree>。
//
// 决策点 4：进程内驱动，不生成独立解析器。
// 决策点 5 B：遇错即停，只报第一个错误，不做 panic-mode 恢复。
//
// 驱动契约（DATA_STRUCTURES §5.6）：
//   - EOF 归一化：输入 token 流可含或不含末尾 EOF；末 token 非 EOF（或为空）时本地追加
//     EOF token（code=0, kind=EndOfFile, name="EOF"）。兼容 Scanner（已含 EOF）与
//     SampleLexReader（不含 EOF），不引入 parser → lexer 依赖。
//   - token.code → 终结符 symbolId 映射：ACTION 键为文法终结符 symbolId，token 用词法编码
//     code；由 Grammar 终结符的 Symbol.tokenCode 建表（GrammarSimplifier 重编号时保留 tokenCode）。
//   - ParseTree.tokens 存归一化后的 token 序列（含 EOF；EOF 为合成 token，无叶子引用）。
//
// 节点构建时序：
//   - Shift → 叶子：symbolId=终结符 id, productionId=-1, tokenIndex=归一化 token 流下标, text=lexeme。
//   - Reduce → 内部节点：symbolId=产生式 lhs, productionId=pid, tokenIndex=-1, text=lhs 符号名；
//     children 顺序必须与产生式右部一致：nodeStack 连续 pop_back 得逆序 [D,C,B]（A -> B C D）
//     → std::reverse → std::move 进 children=[B,C,D]。ε 产生式（rhsSize=0）children 为空。
//   - 根节点 = 接受时 nodeStack 唯一顶层节点（开始符号子树，root->symbolId == g.startSymbol）。
//
// 错误语义（决策点 5 B）：Diagnostic { severity=Error, stage="parse", loc=当前 token.loc }；
//   message 含 tokenIndex、当前 token、期望终结符集合（ACTION[top] keys 的符号名）、当前状态 id；
//   token.code 无终结符映射时 message：无法映射到终结符的 token: code=N lexeme="X"。
//   死循环防御：maxSteps = max(1000, tokens.size() * 1000)。
//
// 失败诊断：stage = "parse"（Severity::Error）：startSymbol 无效 / 产生式为空 / 分析表为空（防御）/
//   状态栈与节点栈不同步（防御）/ token 映射失败 / ACTION 缺省格 / 归约 GOTO 缺省格（防御）/
//   接受时节点栈非 1（防御）/ 超过 maxSteps。

#include <string>
#include <vector>

#include "Grammar.h"
#include "ParseTable.h"
#include "ParseTree.h"
#include "Result.h"

namespace compiler::parser {

class LrDriver {
public:
    // 输入：化简后 Grammar + ParseTableData（T2.6 产出）+ token 流（可含或不含末尾 EOF）。
    // 输出：Result<ParseTree>；失败 stage="parse"，遇错即停只报第一个错误（决策点 5 B）。
    Result<ParseTree> parse(const Grammar& g, const ParseTableData& table,
                            const std::vector<Token>& tokens) const;
};

} // namespace compiler::parser
