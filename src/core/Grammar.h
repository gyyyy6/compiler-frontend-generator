#pragma once

// 文法数据结构（T1.2，依据 docs/DATA_STRUCTURES.md §5）
// 纯 C++17 + STL，不依赖 Qt。
// 本文件只定义数据结构与只读查询，不含任何分析算法
// （First/Follow、LR 项集、ACTION/GOTO 等挂载在任务二算法侧）。

#include <string>
#include <vector>

#include "Symbol.h"

namespace compiler {

// 产生式
struct Production {
    int  id = -1;
    int  lhs = -1;              // 左部 Symbol.id
    std::vector<int> rhs;       // 右部 Symbol.id 序列（Empty 表示 ε 产生式）
    std::string semantic;       // 可选语义动作占位（任务二/三）
};

// 文法：符号表 + 产生式表 + 开始符号
struct Grammar {
    std::vector<Symbol>     symbols;
    std::vector<Production> productions;
    int startSymbol = -1;

    // 按 id 查符号；越界（含 id < 0）返回 nullptr
    const Symbol* symbol(int id) const {
        if (id < 0 || static_cast<size_t>(id) >= symbols.size()) {
            return nullptr;
        }
        return &symbols[static_cast<size_t>(id)];
    }

    // 按名称与类别查符号 id；找不到返回 -1
    int symbolId(const std::string& name, SymbolKind kind) const {
        for (const Symbol& s : symbols) {
            if (s.name == name && s.kind == kind) {
                return s.id;
            }
        }
        return -1;
    }

    // 左部为 lhs 的所有产生式 id，按出现顺序
    std::vector<int> productionsOf(int lhs) const {
        std::vector<int> result;
        for (const Production& p : productions) {
            if (p.lhs == lhs) {
                result.push_back(p.id);
            }
        }
        return result;
    }

    // 全部终结符 id（仅 SymbolKind::Terminal；$ 与 ε 为内部符号，不包含）
    std::vector<int> terminalIds() const {
        std::vector<int> result;
        for (const Symbol& s : symbols) {
            if (s.kind == SymbolKind::Terminal) {
                result.push_back(s.id);
            }
        }
        return result;
    }

    // 全部非终结符 id
    std::vector<int> nonterminalIds() const {
        std::vector<int> result;
        for (const Symbol& s : symbols) {
            if (s.kind == SymbolKind::Nonterminal) {
                result.push_back(s.id);
            }
        }
        return result;
    }
};

} // namespace compiler
