#pragma once

// 非确定有限自动机数据结构（T1.2，依据 docs/DATA_STRUCTURES.md §6）
// 纯 C++17 + STL，不依赖 Qt。构造/转换算法位于 regex/ 模块。

#include <string>
#include <vector>

namespace compiler {

// NFA 边
struct NfaEdge {
    enum class Type : int { Epsilon, Char, CharClass, NegatedClass };
    Type type = Type::Epsilon;
    char ch = 0;              // Char 时有效
    std::string charClass;    // CharClass/NegatedClass 时的字符集合（区间已展开）
    int target = -1;
};

// NFA 状态
struct NfaState {
    int  id = -1;
    bool isAccept = false;
    int  tokenCode = 0;       // 接受态归属的单词编码
    int  priority = 0;        // 规则优先级（.rules 文件顺序，越小越高）
    std::vector<NfaEdge> edges;
};

// NFA：状态表 + 开始状态
struct Nfa {
    std::vector<NfaState> states;
    int startState = -1;
};

} // namespace compiler
