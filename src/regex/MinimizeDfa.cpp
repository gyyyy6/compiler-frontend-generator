#include "MinimizeDfa.h"

#include <algorithm>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace compiler::regex {

namespace {

// 构造 dfa-minimize 阶段的失败结果（不抛异常、不弹窗）
Result<Dfa> minimizeError(const std::string& message) {
    Result<Dfa> result;
    Diagnostic d;
    d.severity = Severity::Error;
    d.stage = "dfa-minimize";
    d.message = message;
    d.loc.line = -1;
    result.diagnostics.push_back(std::move(d));
    return result;
}

// 校验 DFA 合法性：状态表非空、startState 合法、状态 id 与下标一致、转移目标合法
bool validateDfa(const Dfa& dfa, std::string& err) {
    if (dfa.states.empty()) {
        err = "DFA 为空，无法最小化";
        return false;
    }
    if (dfa.startState < 0 ||
        static_cast<size_t>(dfa.startState) >= dfa.states.size()) {
        err = "DFA startState 越界，无法最小化";
        return false;
    }
    for (size_t i = 0; i < dfa.states.size(); ++i) {
        const DfaState& s = dfa.states[i];
        if (s.id != static_cast<int>(i)) {
            err = "DFA 状态 id 与下标不一致（id=" + std::to_string(s.id) +
                  ", 下标=" + std::to_string(i) + "）";
            return false;
        }
        for (const auto& kv : s.transitions) {
            if (kv.second < 0 ||
                static_cast<size_t>(kv.second) >= dfa.states.size()) {
                err = "DFA 转移目标越界（状态 " + std::to_string(i) +
                      " 的字符 '" + kv.first + "'）";
                return false;
            }
        }
    }
    return true;
}

// 接受态初始划分签名：
// 非接受态全部为一类；接受态按 (tokenCode, priority) 分组，
// 保证不同 tokenCode/priority 的等价终态不会合并（合并会改变“报什么 token”的语义）。
struct AcceptSignature {
    bool isAccept = false;
    int  tokenCode = 0;
    int  priority = 0;

    bool operator<(const AcceptSignature& o) const {
        if (isAccept != o.isAccept) return isAccept < o.isAccept;
        if (tokenCode != o.tokenCode) return tokenCode < o.tokenCode;
        return priority < o.priority;
    }
};

AcceptSignature signatureOf(const DfaState& s) {
    AcceptSignature sig;
    sig.isAccept = s.isAccept;
    sig.tokenCode = s.tokenCode;
    sig.priority = s.priority;
    return sig;
}

} // namespace

Result<Dfa> minimizeDfa(const Dfa& dfa) {
    std::string err;
    if (!validateDfa(dfa, err)) {
        return minimizeError(err);
    }

    const int n = static_cast<int>(dfa.states.size());

    // 接受态为空：直接返回原 DFA（不划分、不改语言语义）
    bool anyAccept = false;
    for (const DfaState& s : dfa.states) {
        if (s.isAccept) {
            anyAccept = true;
            break;
        }
    }
    if (!anyAccept) {
        return Result<Dfa>::success(dfa);
    }

    // ---- 1) 初始划分：按接受签名分组 ----
    std::map<AcceptSignature, int> blockBySig;
    std::vector<std::vector<int>> blocks;   // 每块：升序状态 id
    std::vector<int> blockOf(n, -1);        // 状态 → 块索引
    for (int s = 0; s < n; ++s) {
        const AcceptSignature sig = signatureOf(dfa.states[static_cast<size_t>(s)]);
        auto it = blockBySig.find(sig);
        int b;
        if (it == blockBySig.end()) {
            b = static_cast<int>(blocks.size());
            blockBySig[sig] = b;
            blocks.emplace_back();
        } else {
            b = it->second;
        }
        blocks[static_cast<size_t>(b)].push_back(s);
        blockOf[static_cast<size_t>(s)] = b;
    }

    // 逆转移表：inverse[c][t] = 沿 c 进入 t 的源状态列表（升序）
    std::map<char, std::vector<std::vector<int>>> inverse;
    for (const DfaState& s : dfa.states) {
        for (const auto& kv : s.transitions) {
            inverse[kv.first].resize(static_cast<size_t>(n));
            inverse[kv.first][static_cast<size_t>(kv.second)].push_back(s.id);
        }
    }

    // ---- 2) Hopcroft 精化 ----
    // 时间戳标记：避免每次清空 mark / blockTouched 数组
    std::vector<int> mark(n, -1);
    std::vector<int> blockTouched(blocks.size(), -1);
    int generation = 0;

    // 工作列表：初始为全部块
    std::deque<int> worklist;
    std::vector<char> inWorklist(blocks.size(), true);
    for (size_t b = 0; b < blocks.size(); ++b) {
        worklist.push_back(static_cast<int>(b));
    }

    while (!worklist.empty()) {
        const int A = worklist.front();
        worklist.pop_front();
        inWorklist[static_cast<size_t>(A)] = false;

        // 快照：A 自身在精化中可能被分裂，不能持有 blocks[A] 的引用
        const std::vector<int> splitter = blocks[static_cast<size_t>(A)];

        for (char c : dfa.alphabet) {
            ++generation;

            // X = 沿 c 进入 splitter 的状态集合
            const auto invIt = inverse.find(c);
            if (invIt == inverse.end()) {
                continue;   // 字母表内但无任何 c 转移：X 为空，无精化
            }
            const std::vector<std::vector<int>>& invC = invIt->second;
            std::vector<int> markedStates;
            for (int t : splitter) {
                for (int s : invC[static_cast<size_t>(t)]) {
                    if (mark[static_cast<size_t>(s)] != generation) {
                        mark[static_cast<size_t>(s)] = generation;
                        markedStates.push_back(s);
                    }
                }
            }
            if (markedStates.empty()) {
                continue;
            }

            // 候选块：含被标记状态的块（同一代内去重）
            std::vector<int> candidates;
            for (int s : markedStates) {
                const int b = blockOf[static_cast<size_t>(s)];
                if (blockTouched[static_cast<size_t>(b)] != generation) {
                    blockTouched[static_cast<size_t>(b)] = generation;
                    candidates.push_back(b);
                }
            }

            for (int b : candidates) {
                const std::vector<int> Y = blocks[static_cast<size_t>(b)];

                // 分裂 Y = (Y ∩ X) ∪ (Y \ X)
                std::vector<int> inX, notInX;
                inX.reserve(Y.size());
                notInX.reserve(Y.size());
                for (int s : Y) {
                    if (mark[static_cast<size_t>(s)] == generation) {
                        inX.push_back(s);
                    } else {
                        notInX.push_back(s);
                    }
                }
                if (inX.empty() || notInX.empty()) {
                    continue;   // 未真正分裂
                }

                // 较大一半留在原块，较小一半作为新块（保证“较小一半入队”的复杂度性质）
                std::vector<int>& larger =
                    (inX.size() >= notInX.size()) ? inX : notInX;
                std::vector<int>& smaller =
                    (inX.size() >= notInX.size()) ? notInX : inX;
                blocks[static_cast<size_t>(b)] = std::move(larger);

                const int nb = static_cast<int>(blocks.size());
                blocks.push_back(std::move(smaller));
                for (int s : blocks[static_cast<size_t>(nb)]) {
                    blockOf[static_cast<size_t>(s)] = nb;
                }
                inWorklist.push_back(false);
                blockTouched.push_back(-1);

                if (inWorklist[static_cast<size_t>(b)]) {
                    // b 已在工作列表：分裂后用 b + nb 两个块替换
                    inWorklist[static_cast<size_t>(nb)] = true;
                    worklist.push_back(b);
                    worklist.push_back(nb);
                } else {
                    // b 不在工作列表：只把较小的新块 nb 加入
                    inWorklist[static_cast<size_t>(nb)] = true;
                    worklist.push_back(nb);
                }
            }
        }
    }

    // ---- 3) 构造最小化 DFA ----
    // 块按“最小状态 id”排序，得到确定性的新状态编号
    std::vector<int> blockOrder;
    blockOrder.reserve(blocks.size());
    for (size_t b = 0; b < blocks.size(); ++b) {
        blockOrder.push_back(static_cast<int>(b));
    }
    std::sort(blockOrder.begin(), blockOrder.end(), [&](int a, int b) {
        return blocks[static_cast<size_t>(a)].front() <
               blocks[static_cast<size_t>(b)].front();
    });

    std::vector<int> newIdOfBlock(blocks.size(), -1);
    for (size_t k = 0; k < blockOrder.size(); ++k) {
        newIdOfBlock[static_cast<size_t>(blockOrder[k])] = static_cast<int>(k);
    }

    Dfa out;
    out.alphabet = dfa.alphabet;
    out.states.resize(blocks.size());

    for (size_t k = 0; k < blockOrder.size(); ++k) {
        const int b = blockOrder[k];
        const std::vector<int>& members = blocks[static_cast<size_t>(b)];

        DfaState ns;
        ns.id = static_cast<int>(k);
        ns.isAccept = dfa.states[static_cast<size_t>(members.front())].isAccept;

        // 终态合并语义：取 priority 最小者（并列取 tokenCode 最小者）的 tokenCode/priority。
        // 初始划分已保证块内终态签名一致；此规则与 T1.4 finalizeAccept 保持一致，
        // 作为稳健保障（若未来放宽初始划分仍保持正确语义）。
        if (ns.isAccept) {
            bool found = false;
            int bestPriority = 0;
            int bestTokenCode = 0;
            for (int s : members) {
                const DfaState& st = dfa.states[static_cast<size_t>(s)];
                if (!st.isAccept) {
                    continue;
                }
                if (!found || st.priority < bestPriority ||
                    (st.priority == bestPriority && st.tokenCode < bestTokenCode)) {
                    found = true;
                    bestPriority = st.priority;
                    bestTokenCode = st.tokenCode;
                }
            }
            ns.priority = bestPriority;
            ns.tokenCode = bestTokenCode;
        }

        // 转移：Hopcroft 稳定性保证块内状态同字符转移目标同块，
        // 取代表（最小 id）状态的转移即可；缺失转移保持缺失（部分 DFA 语义不变）
        const DfaState& rep = dfa.states[static_cast<size_t>(members.front())];
        for (const auto& kv : rep.transitions) {
            const int targetBlock = blockOf[static_cast<size_t>(kv.second)];
            ns.transitions[kv.first] =
                newIdOfBlock[static_cast<size_t>(targetBlock)];
        }

        // 合并 nfaStates（可视化/调试用）：块内所有成员子集的并集
        for (int s : members) {
            const std::vector<int>& src = dfa.states[static_cast<size_t>(s)].nfaStates;
            ns.nfaStates.insert(ns.nfaStates.end(), src.begin(), src.end());
        }
        std::sort(ns.nfaStates.begin(), ns.nfaStates.end());
        ns.nfaStates.erase(
            std::unique(ns.nfaStates.begin(), ns.nfaStates.end()),
            ns.nfaStates.end());

        out.states[static_cast<size_t>(k)] = std::move(ns);
    }

    out.startState =
        newIdOfBlock[static_cast<size_t>(blockOf[static_cast<size_t>(dfa.startState)])];

    return Result<Dfa>::success(std::move(out));
}

} // namespace compiler::regex
