#include "RegexToNfa.h"

#include <utility>

namespace compiler::regex {

namespace {

// Thompson 片段：由 start 与 accept 两个状态界定
struct Fragment {
    int start = -1;
    int accept = -1;
};

class NfaBuilder {
public:
    NfaBuilder() = default;

    Result<Nfa> build(const RegexNode& ast) {
        const Fragment f = buildFragment(ast);
        if (failed_) {
            Result<Nfa> result;
            result.diagnostics = std::move(diagnostics_);
            return result;
        }
        nfa_.startState = f.start;
        nfa_.states[static_cast<size_t>(f.accept)].isAccept = true;
        return Result<Nfa>::success(std::move(nfa_));
    }

private:
    int newState() {
        NfaState s;
        s.id = static_cast<int>(nfa_.states.size());
        nfa_.states.push_back(std::move(s));
        return s.id;
    }

    void addEdge(int from, int to, NfaEdge edge) {
        edge.target = to;
        nfa_.states[static_cast<size_t>(from)].edges.push_back(std::move(edge));
    }

    void addEpsilon(int from, int to) {
        NfaEdge e;
        e.type = NfaEdge::Type::Epsilon;
        addEdge(from, to, std::move(e));
    }

    // 单字符边片段
    Fragment makeCharFragment(char c) {
        const int s = newState();
        const int a = newState();
        NfaEdge e;
        e.type = NfaEdge::Type::Char;
        e.ch = c;
        addEdge(s, a, std::move(e));
        return { s, a };
    }

    // 字符类/反义类边片段
    Fragment makeClassFragment(NfaEdge::Type type, std::string charClass) {
        const int s = newState();
        const int a = newState();
        NfaEdge e;
        e.type = type;
        e.charClass = std::move(charClass);
        addEdge(s, a, std::move(e));
        return { s, a };
    }

    Fragment buildFragment(const RegexNode& n) {
        switch (n.kind) {
            case RegexNodeKind::Empty: {
                const int s = newState();
                return { s, s };   // 单状态：既是开始也是接受
            }
            case RegexNodeKind::Char:
                return makeCharFragment(n.ch);
            case RegexNodeKind::CharClass:
                return makeClassFragment(NfaEdge::Type::CharClass, n.charClass);
            case RegexNodeKind::NegatedClass:
                return makeClassFragment(NfaEdge::Type::NegatedClass, n.charClass);
            case RegexNodeKind::Dot:
                // 任意字符 = 空反义类 [^]
                return makeClassFragment(NfaEdge::Type::NegatedClass, "");
            case RegexNodeKind::Concat: {
                if (n.children.empty()) {
                    const int s = newState();
                    return { s, s };
                }
                Fragment result = buildFragment(*n.children[0]);
                for (size_t i = 1; i < n.children.size() && !failed_; ++i) {
                    const Fragment next = buildFragment(*n.children[i]);
                    if (failed_) {
                        return { -1, -1 };
                    }
                    addEpsilon(result.accept, next.start);
                    result.accept = next.accept;
                }
                return result;
            }
            case RegexNodeKind::Alternate: {
                const int s = newState();
                const int a = newState();
                for (const auto& child : n.children) {
                    const Fragment f = buildFragment(*child);
                    if (failed_) {
                        return { -1, -1 };
                    }
                    addEpsilon(s, f.start);
                    addEpsilon(f.accept, a);
                }
                return { s, a };
            }
            case RegexNodeKind::Star: {
                if (n.children.empty()) {
                    error("Star 节点缺少操作数");
                    return { -1, -1 };
                }
                const int s = newState();
                const int a = newState();
                const Fragment f = buildFragment(*n.children[0]);
                if (failed_) {
                    return { -1, -1 };
                }
                addEpsilon(s, f.start);        // 进入循环体
                addEpsilon(f.accept, f.start); // 循环
                addEpsilon(s, a);              // 零次
                addEpsilon(f.accept, a);       // 一次后退出
                return { s, a };
            }
            case RegexNodeKind::Plus: {
                if (n.children.empty()) {
                    error("Plus 节点缺少操作数");
                    return { -1, -1 };
                }
                const int s = newState();
                const int a = newState();
                const Fragment f = buildFragment(*n.children[0]);
                if (failed_) {
                    return { -1, -1 };
                }
                addEpsilon(s, f.start);        // 至少一次
                addEpsilon(f.accept, f.start); // 可继续
                addEpsilon(f.accept, a);       // 退出
                return { s, a };
            }
            case RegexNodeKind::Optional: {
                if (n.children.empty()) {
                    error("Optional 节点缺少操作数");
                    return { -1, -1 };
                }
                const int s = newState();
                const int a = newState();
                const Fragment f = buildFragment(*n.children[0]);
                if (failed_) {
                    return { -1, -1 };
                }
                addEpsilon(s, f.start);
                addEpsilon(f.accept, a);
                addEpsilon(s, a);              // 零次
                return { s, a };
            }
            case RegexNodeKind::Negate: {
                // 解析期已保证操作数为字符级原子；此处再防御性处理
                if (n.children.empty()) {
                    error("Negate 节点缺少操作数");
                    return { -1, -1 };
                }
                const RegexNode& op = *n.children[0];
                switch (op.kind) {
                    case RegexNodeKind::Char:
                        return makeClassFragment(NfaEdge::Type::NegatedClass,
                                                 std::string(1, op.ch));
                    case RegexNodeKind::CharClass:
                        return makeClassFragment(NfaEdge::Type::NegatedClass,
                                                 op.charClass);
                    case RegexNodeKind::NegatedClass:
                        return makeClassFragment(NfaEdge::Type::CharClass,
                                                 op.charClass);
                    case RegexNodeKind::Dot:
                        // 任意字符的补集 = 空语言
                        return makeClassFragment(NfaEdge::Type::CharClass, "");
                    case RegexNodeKind::Negate:
                        return buildFragment(op);   // 双重否定
                    default:
                        error("'~' 的操作数不是字符级原子");
                        return { -1, -1 };
                }
            }
        }
        error("未知的 AST 节点类型");
        return { -1, -1 };
    }

    void error(const std::string& message) {
        if (failed_) {
            return;
        }
        failed_ = true;
        Diagnostic d;
        d.severity = Severity::Error;
        d.stage = "nfa-build";
        d.message = message;
        d.loc.line = -1;   // 无位置
        diagnostics_.push_back(std::move(d));
    }

    Nfa nfa_;
    bool failed_ = false;
    std::vector<Diagnostic> diagnostics_;
};

} // namespace

// ---- 公开 API ----

Result<Nfa> buildNfa(const RegexNode& ast) {
    return NfaBuilder().build(ast);
}

Result<Nfa> buildNfaFromRegex(const std::string& pattern,
                              const RegexNameTable& names) {
    Result<std::unique_ptr<RegexNode>> parsed = parseRegex(pattern, names);
    if (!parsed.ok) {
        Result<Nfa> result;
        result.diagnostics = std::move(parsed.diagnostics);
        return result;
    }
    return buildNfa(*parsed.value);
}

} // namespace compiler::regex
