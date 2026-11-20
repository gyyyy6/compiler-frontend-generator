#include "NfaToDfa.h"

#include <deque>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace compiler::regex {

namespace {

using Subset = std::set<int>;

// 状态 id 是否可作为 nfa.states 的下标（要求 id == 下标且不越界）
bool isValidState(const Nfa& nfa, int id) {
    return id >= 0 && static_cast<size_t>(id) < nfa.states.size();
}

// 构造 dfa-build 阶段的失败结果（不抛异常、不弹窗）
Result<Dfa> dfaBuildError(const std::string& message) {
    Result<Dfa> result;
    Diagnostic d;
    d.severity = Severity::Error;
    d.stage = "dfa-build";
    d.message = message;
    d.loc.line = -1;
    result.diagnostics.push_back(std::move(d));
    return result;
}

// 边是否接受输入字符 c（Epsilon 由 ε-闭包处理，这里恒为 false）
bool edgeMatches(const NfaEdge& e, char c) {
    switch (e.type) {
        case NfaEdge::Type::Char:         return e.ch == c;
        case NfaEdge::Type::CharClass:    return e.charClass.find(c) != std::string::npos;
        case NfaEdge::Type::NegatedClass: return e.charClass.find(c) == std::string::npos;
        case NfaEdge::Type::Epsilon:      return false;
    }
    return false;
}

// ε-闭包：状态集合沿 ε 边可到达的全部状态；遇非法状态 id 返回 false
bool epsilonClosure(const Nfa& nfa, const Subset& states, Subset& result) {
    result = states;
    std::vector<int> stack(states.begin(), states.end());
    while (!stack.empty()) {
        const int s = stack.back();
        stack.pop_back();
        // 入栈前校验：s 越界则下方 nfa.states[s] 会越界
        if (!isValidState(nfa, s)) {
            return false;
        }
        for (const NfaEdge& e : nfa.states[static_cast<size_t>(s)].edges) {
            if (e.type != NfaEdge::Type::Epsilon) {
                continue;
            }
            // e.target 写入 result / stack 前校验
            if (!isValidState(nfa, e.target)) {
                return false;
            }
            if (result.insert(e.target).second) {
                stack.push_back(e.target);
            }
        }
    }
    return true;
}

// 单字符 move：状态集合中所有匹配 c 的边之目标状态；遇非法状态 id 返回 false
bool moveOn(const Nfa& nfa, const Subset& states, char c, Subset& out) {
    out.clear();
    for (int s : states) {
        if (!isValidState(nfa, s)) {
            return false;
        }
        for (const NfaEdge& e : nfa.states[static_cast<size_t>(s)].edges) {
            if (edgeMatches(e, c)) {
                // out.insert(e.target) 前校验目标状态
                if (!isValidState(nfa, e.target)) {
                    return false;
                }
                out.insert(e.target);
            }
        }
    }
    return true;
}

// 收集字母表：Char 边字符、CharClass 内字符、NegatedClass 的补集
// （字节域 0..255 中不在排除集合内的字符）
std::set<char> collectAlphabet(const Nfa& nfa) {
    std::set<char> alpha;
    for (const NfaState& s : nfa.states) {
        for (const NfaEdge& e : s.edges) {
            switch (e.type) {
                case NfaEdge::Type::Char:
                    alpha.insert(e.ch);
                    break;
                case NfaEdge::Type::CharClass:
                    for (char c : e.charClass) {
                        alpha.insert(c);
                    }
                    break;
                case NfaEdge::Type::NegatedClass:
                    for (int i = 0; i < 256; ++i) {
                        const char c = static_cast<char>(i);
                        if (e.charClass.find(c) == std::string::npos) {
                            alpha.insert(c);
                        }
                    }
                    break;
                case NfaEdge::Type::Epsilon:
                    break;
            }
        }
    }
    return alpha;
}

// 从 NFA 状态子集合并接受态：
// 多个 NFA 接受态指向同一 DFA 状态时，取 priority 最小（并列取 tokenCode 最小）；
// 遇非法状态 id 返回 false
bool finalizeAccept(const Nfa& nfa, DfaState& ds) {
    bool found = false;
    int bestPriority = 0;
    int bestTokenCode = 0;
    for (int ns : ds.nfaStates) {
        if (!isValidState(nfa, ns)) {
            return false;
        }
        const NfaState& s = nfa.states[static_cast<size_t>(ns)];
        if (!s.isAccept) {
            continue;
        }
        if (!found || s.priority < bestPriority ||
            (s.priority == bestPriority && s.tokenCode < bestTokenCode)) {
            found = true;
            bestPriority = s.priority;
            bestTokenCode = s.tokenCode;
        }
    }
    if (found) {
        ds.isAccept = true;
        ds.tokenCode = bestTokenCode;
        ds.priority = bestPriority;
    }
    return true;
}

} // namespace

Result<Dfa> subsetConstruct(const Nfa& nfa) {
    if (nfa.states.empty()) {
        return dfaBuildError("NFA 为空，无法构造 DFA");
    }
    if (!isValidState(nfa, nfa.startState)) {
        return dfaBuildError("NFA startState 越界，无法构造 DFA");
    }

    Dfa dfa;
    dfa.alphabet = collectAlphabet(nfa);

    std::map<Subset, int> subsetToId;
    std::vector<Subset> subsetsById;

    // 初始 DFA 状态 = 开始状态的 ε-闭包
    {
        Subset startSubset;
        if (!epsilonClosure(nfa, Subset{ nfa.startState }, startSubset)) {
            return dfaBuildError("NFA 状态 id 越界：计算初始 ε-闭包失败");
        }
        subsetToId[startSubset] = 0;
        subsetsById.push_back(startSubset);
        DfaState s0;
        s0.id = 0;
        s0.nfaStates.assign(startSubset.begin(), startSubset.end());
        if (!finalizeAccept(nfa, s0)) {
            return dfaBuildError("NFA 状态 id 越界：合并初始接受态失败");
        }
        dfa.states.push_back(std::move(s0));
        dfa.startState = 0;
    }

    // BFS 工作列表（按字母表升序展开，保证确定性）
    std::deque<int> worklist{ 0 };
    while (!worklist.empty()) {
        const int id = worklist.front();
        worklist.pop_front();
        // 防御性检查：内部 id 必须是 subsetsById / dfa.states 的合法下标
        if (id < 0 ||
            static_cast<size_t>(id) >= subsetsById.size() ||
            static_cast<size_t>(id) >= dfa.states.size()) {
            return dfaBuildError("内部错误：BFS 工作列表 id 越界");
        }
        // 拷贝子集：下方 push_back 可能使 subsetsById 重新分配，不能持有引用
        const Subset T = subsetsById[static_cast<size_t>(id)];

        for (char c : dfa.alphabet) {
            Subset moved;
            if (!moveOn(nfa, T, c, moved)) {
                return dfaBuildError("NFA 状态 id 越界：move 失败");
            }
            Subset U;
            if (!epsilonClosure(nfa, moved, U)) {
                return dfaBuildError("NFA 状态 id 越界：ε-闭包失败");
            }
            if (U.empty()) {
                continue;   // 无转移
            }
            const auto it = subsetToId.find(U);
            int uid;
            if (it == subsetToId.end()) {
                uid = static_cast<int>(dfa.states.size());
                if (static_cast<size_t>(uid) != dfa.states.size()) {
                    return dfaBuildError("内部错误：DFA 状态数超出 int 范围");
                }
                subsetToId[U] = uid;
                subsetsById.push_back(U);
                DfaState ds;
                ds.id = uid;
                ds.nfaStates.assign(U.begin(), U.end());
                if (!finalizeAccept(nfa, ds)) {
                    return dfaBuildError("NFA 状态 id 越界：合并接受态失败");
                }
                dfa.states.push_back(std::move(ds));
                worklist.push_back(uid);
            } else {
                uid = it->second;
            }
            // 防御性检查：转移目标必须已存在
            if (static_cast<size_t>(uid) >= dfa.states.size()) {
                return dfaBuildError("内部错误：DFA 转移目标 id 越界");
            }
            dfa.states[static_cast<size_t>(id)].transitions[c] = uid;
        }
    }

    return Result<Dfa>::success(std::move(dfa));
}

} // namespace compiler::regex
