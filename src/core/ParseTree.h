#pragma once

// 语法树数据结构（T1.2，依据 docs/DATA_STRUCTURES.md §7）
// 纯 C++17 + STL，不依赖 Qt。树持有参与推导的 Token 序列，
// 叶子通过 tokenIndex 引用，不复制 Token 对象。

#include <memory>
#include <string>
#include <vector>

#include "Token.h"

namespace compiler {

// 语法树节点
struct ParseTreeNode {
    int  symbolId = -1;        // 本节点符号（终结符或非终结符）
    int  productionId = -1;    // 非终结符展开所用的产生式 id；叶子为 -1
    int  tokenIndex = -1;      // 终结符叶子：所属 Token 数组下标；非叶子为 -1
    std::string text;          // 显示文本（lexeme 或符号名）
    std::vector<std::unique_ptr<ParseTreeNode>> children;

    bool isLeaf() const { return children.empty(); }
};

// 一棵语法树：树持有参与推导的 Token 序列，叶子通过 tokenIndex 引用
struct ParseTree {
    std::vector<Token> tokens;
    std::unique_ptr<ParseTreeNode> root;
};

} // namespace compiler
