#include "LexerBuilder.h"

#include "RegexToNfa.h"    // regex::buildNfa
#include "NfaToDfa.h"      // regex::subsetConstruct
#include "MinimizeDfa.h"   // regex::minimizeDfa

#include <queue>
#include <utility>
#include <vector>

namespace compiler::lexer {

namespace {

Result<Nfa> nfaBuildError(const std::string& msg) {
    Result<Nfa> r;
    Diagnostic d;
    d.severity = Severity::Error;
    d.stage = "nfa-build";
    d.message = msg;
    d.loc.line = -1;
    r.diagnostics.push_back(std::move(d));
    return r;
}

// ε-闭包：从 start 出发沿 ε 边可到达的全部状态（含自身）
std::vector<int> epsilonClosure(const Nfa& nfa, const std::vector<int>& start) {
    std::vector<int> closure;
    std::vector<bool> seen(nfa.states.size(), false);
    std::queue<int> q;
    for (int s : start) {
        if (s < 0 || static_cast<size_t>(s) >= nfa.states.size()) continue;
        if (seen[static_cast<size_t>(s)]) continue;
        seen[static_cast<size_t>(s)] = true;
        closure.push_back(s);
        q.push(s);
    }
    while (!q.empty()) {
        const int s = q.front();
        q.pop();
        for (const NfaEdge& e : nfa.states[static_cast<size_t>(s)].edges) {
            if (e.type != NfaEdge::Type::Epsilon) continue;
            if (e.target < 0 || static_cast<size_t>(e.target) >= nfa.states.size()) continue;
            if (seen[static_cast<size_t>(e.target)]) continue;
            seen[static_cast<size_t>(e.target)] = true;
            closure.push_back(e.target);
            q.push(e.target);
        }
    }
    return closure;
}

} // namespace

Result<Nfa> LexerBuilder::buildNfa(const LexerSpec& spec) {
    Nfa merged;
    merged.startState = 0;
    merged.states.push_back(NfaState{0, false, 0, 0, {}});   // 状态 0：公共起点

    for (const LexerRule& rule : spec.rules) {
        // 1) 【关键】显式跳过关键字：只登记编码、不生成 DFA（regex=nullptr）
        if (rule.isKeyword) {
            continue;
        }

        // 2) 非 keyword 规则必须有 AST；缺失 → 失败诊断（不崩溃、不静默跳过）
        if (!rule.regex) {
            return nfaBuildError("规则 '" + rule.name + "' 缺少正则 AST，无法生成 DFA");
        }

        // 3) 单规则 NFA（失败诊断 stage 已为 "nfa-build"，直接透传）
        Result<Nfa> single = regex::buildNfa(*rule.regex);
        if (!single.ok) {
            Result<Nfa> r;
            r.diagnostics = std::move(single.diagnostics);
            return r;
        }
        Nfa& sn = single.value;
        if (sn.states.empty() || sn.startState < 0 ||
            static_cast<size_t>(sn.startState) >= sn.states.size()) {
            return nfaBuildError("规则 '" + rule.name + "' 生成的 NFA 非法（起点缺失）");
        }

        // 4) 状态 id 重编号：offset = 当前 merged.states.size()；
        //    每个独立 NFA 的 state.id / edge.target 均从 0 起，整体平移后追加。
        const int offset = static_cast<int>(merged.states.size());
        for (NfaState& st : sn.states) {
            st.id += offset;
            for (NfaEdge& e : st.edges) {
                e.target += offset;
            }
            if (st.isAccept) {          // 接受态打戳记
                st.tokenCode = rule.code;
                st.priority = rule.priority;
            }
        }

        // 5) 共起点 ε 边：公共起点(0) → 该规则平移后的起点
        merged.states[0].edges.push_back(
            NfaEdge{NfaEdge::Type::Epsilon, 0, "", offset + sn.startState});

        // 6) 追加到总 NFA
        for (NfaState& st : sn.states) {
            merged.states.push_back(std::move(st));
        }
    }

    // 防御性校验：无任何生成 DFA 的规则（全部为 Keyword 或缺失 regex）→ 明确报错
    if (merged.states[0].edges.empty()) {
        return nfaBuildError(
            "LexerSpec 没有任何可生成 DFA 的规则（全部为 Keyword 或缺失 regex），"
            "至少需要一条非 Keyword 前缀的规则参与扫描。");
    }

    // 7) 空串校验：公共起点 ε-闭包含任一接受态 → 某条"生成 DFA 的规则"可接受空串。
    //    只针对有 regex 的规则（keyword 不在总 NFA 中，不参与判断）。
    const std::vector<int> closure = epsilonClosure(merged, {merged.startState});
    for (int s : closure) {
        if (merged.states[static_cast<size_t>(s)].isAccept) {
            return nfaBuildError("存在可接受空串的规则（会造成扫描死循环），请检查 .rules");
        }
    }

    return Result<Nfa>::success(std::move(merged));
}

Result<Dfa> LexerBuilder::buildDfa(const LexerSpec& spec) {
    Result<Nfa> nfa = buildNfa(spec);
    if (!nfa.ok) {
        Result<Dfa> r;
        r.diagnostics = std::move(nfa.diagnostics);
        return r;
    }
    return regex::subsetConstruct(nfa.value);
}

Result<Dfa> LexerBuilder::buildMinimalDfa(const LexerSpec& spec) {
    Result<Dfa> dfa = buildDfa(spec);
    if (!dfa.ok) {
        Result<Dfa> r;
        r.diagnostics = std::move(dfa.diagnostics);
        return r;
    }
    return regex::minimizeDfa(dfa.value);
}

} // namespace compiler::lexer