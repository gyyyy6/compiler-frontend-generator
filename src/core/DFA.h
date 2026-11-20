#pragma once

// 确定有限自动机数据结构（T1.2，依据 docs/DATA_STRUCTURES.md §6）
// 纯 C++17 + STL，不依赖 Qt。子集构造/最小化算法位于 regex/ 模块。

#include <map>
#include <set>
#include <vector>

namespace compiler {

// DFA 状态
struct DfaState {
    int  id = -1;
    bool isAccept = false;
    int  tokenCode = 0;       // 接受态归属的单词编码
    int  priority = 0;        // 多个 NFA 接受态合并时取最小 priority
    std::map<char, int> transitions;   // 输入字符 → 目标状态；缺省表示无转移（报错）
    std::vector<int> nfaStates;        // 对应 NFA 状态子集（可视化/调试用）
};

// DFA：状态表 + 开始状态 + 字母表
struct Dfa {
    std::vector<DfaState> states;
    int startState = -1;
    std::set<char> alphabet;   // 所有参与转移的字符
};

} // namespace compiler
