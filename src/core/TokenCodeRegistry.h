#pragma once

// 单词编码登记表（T1.2，依据 docs/DATA_STRUCTURES.md §4）
// 纯 C++17 + STL，不依赖 Qt。
// 保留编码：0 = EOF（display "$"）、255 = ERROR（display "ERROR"），
// 均不占用 .rules 分配序列；登记表始终内置这两个保留项。

#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "Token.h"

namespace compiler {

// 单词类型登记项
struct TokenType {
    int         code    = 0;
    std::string name;        // 登记名："KW_IF"、"ID"、"NUM"、"SYM_PLUS"...
    TokenKind   kind    = TokenKind::Unknown;
    std::string display;     // 显示/回写形式："if"、"ID"、"$"、"ERROR"
};

// 单词编码登记表：code ↔ name 双向查询，并可序列化为 sample.lex 头部
class TokenCodeRegistry {
public:
    // 保留编码（DATA_STRUCTURES.md §4）
    static constexpr int kEofCode   = 0;
    static constexpr int kErrorCode = 255;

    TokenCodeRegistry() { resetToReservedOnly(); }

    // 按 .rules 文件出现顺序分配 1, 2, 3, ...（跳过保留编码与已用编码），返回分配的 code。
    // t.code 字段被忽略；name 为保留名（EOF/ERROR）时返回对应保留编码；
    // name 已存在时幂等返回原 code；display 为空时回退为 name；编码耗尽（>=255）返回 -1。
    int registerToken(const TokenType& t) {
        if (t.name == "EOF" || t.name == "ERROR") {
            return codeOf(t.name);
        }
        const auto existing = byName_.find(t.name);
        if (existing != byName_.end()) {
            return existing->second;   // 幂等
        }
        TokenType type = t;
        if (type.display.empty()) {
            type.display = type.name;
        }
        type.code = allocateCode();
        if (type.code < 0) {
            return -1;                 // 无可用编码
        }
        byCode_.emplace(type.code, type);
        byName_.emplace(type.name, type.code);
        return type.code;
    }

    // 显式编码登记（T1.6，.rules 的 Name_<code> / Name<code> / Name<code>L 形式）：
    // 校验 code∈[1,254] 且 code、name 均未被占用；成功返回 code，否则返回 -1。
    // 不推进 nextCode_（单调递增仅由 allocateCode 维护，分配时自动跳过本处已用编码），
    // 即：先显式登记 100 后 nextCode_ 保持 1，后续自动分配依次得 1、2、...。
    int registerToken(const TokenType& t, int explicitCode) {
        if (explicitCode <= kEofCode || explicitCode >= kErrorCode) {
            return -1;                 // 0 与 255 保留
        }
        if (byName_.count(t.name) != 0 || byCode_.count(explicitCode) != 0) {
            return -1;                 // 名称或编码重复
        }
        TokenType type = t;
        type.code = explicitCode;
        if (type.display.empty()) {
            type.display = type.name;
        }
        byCode_.emplace(explicitCode, std::move(type));
        byName_.emplace(t.name, explicitCode);
        return explicitCode;
    }

    // 按名称查编码；找不到返回 -1
    int codeOf(const std::string& name) const {
        const auto it = byName_.find(name);
        return it == byName_.end() ? -1 : it->second;
    }

    // 按编码查类型；找不到返回 nullptr
    const TokenType* typeOf(int code) const {
        const auto it = byCode_.find(code);
        return it == byCode_.end() ? nullptr : &it->second;
    }

    // 下一个可用编码（保留项不计入；起始为 1）
    int nextCode() const { return nextCode_; }

    // 清空全部用户登记项，仅保留 EOF/ERROR 两个保留项
    void clear() { resetToReservedOnly(); }

    // 序列化 sample.lex 头部的 token 表部分（FILE_FORMATS.md §4.2）：
    //   #token-count: N
    //   #token 0 EOF $
    //   #token <code> <name> <display>
    //   ...
    //   #token 255 ERROR ERROR
    //   #token-table-end
    // #language / #source / #generator 等元数据行由调用方（codegen/扫描器）负责。
    std::string toHeader() const {
        std::string out;
        out += "#token-count: ";
        out += std::to_string(byCode_.size() - 2);   // 减去 EOF/ERROR 保留项
        out += "\n";
        for (const auto& entry : byCode_) {
            out += "#token ";
            out += std::to_string(entry.second.code);
            out += ' ';
            out += entry.second.name;
            out += ' ';
            out += entry.second.display;
            out += '\n';
        }
        out += "#token-table-end\n";
        return out;
    }

    // 从 sample.lex 头部文本重建登记表（忽略非 #token 行；格式非法返回 false）
    bool fromHeader(const std::string& text) {
        clear();
        int maxCode = 0;
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();   // 兼容 CRLF
            }
            if (line.rfind("#token ", 0) != 0) {
                continue;          // 跳过元数据行
            }
            std::istringstream ls(line.substr(7));   // 去掉 "#token "
            int code = 0;
            std::string name;
            std::string display;
            if (!(ls >> code >> name >> display)) {
                return false;
            }
            if (code == kEofCode) {
                if (name != "EOF") return false;
                continue;          // 保留项已在 clear() 中重建
            }
            if (code == kErrorCode) {
                if (name != "ERROR") return false;
                continue;
            }
            if (byCode_.count(code) != 0 || byName_.count(name) != 0) {
                return false;      // 编码或名称重复
            }
            TokenType type;
            type.code    = code;
            type.name    = name;
            type.kind    = inferKindFromName(name, display);
            type.display = display;
            byCode_.emplace(code, std::move(type));
            byName_.emplace(name, code);
            if (code > maxCode) {
                maxCode = code;
            }
        }
        nextCode_ = maxCode + 1;
        if (nextCode_ == kErrorCode) {
            ++nextCode_;           // 跳过保留编码 255
        }
        return true;
    }

    // 全部登记项（含保留项），按 code 升序
    std::vector<const TokenType*> orderedTypes() const {
        std::vector<const TokenType*> result;
        result.reserve(byCode_.size());
        for (const auto& entry : byCode_) {
            result.push_back(&entry.second);
        }
        return result;
    }

private:
    // 自动分配：返回 >= nextCode_ 的最小未用编码（跳过 255 与所有已用编码）；
    // nextCode_ 单调递增；无可用编码（>=255）返回 -1。
    // 例：已用 1、3 时依次分配 2、4；先显式登记 100 后自动分配仍从 1 起。
    int allocateCode() {
        while (nextCode_ < kErrorCode && byCode_.count(nextCode_) != 0) {
            ++nextCode_;
        }
        if (nextCode_ >= kErrorCode) {
            return -1;
        }
        return nextCode_++;
    }

    // 仅保留 EOF/ERROR 两个保留项
    void resetToReservedOnly() {
        byCode_.clear();
        byName_.clear();
        nextCode_ = 1;
        byCode_.emplace(kEofCode, TokenType{kEofCode, "EOF", TokenKind::EndOfFile, "$"});
        byName_.emplace("EOF", kEofCode);
        byCode_.emplace(kErrorCode, TokenType{kErrorCode, "ERROR", TokenKind::Error, "ERROR"});
        byName_.emplace("ERROR", kErrorCode);
    }

    // 头部文本不含 kind 字段，按名称前缀启发式推断（仅用于展示/着色）
    static TokenKind inferKindFromName(const std::string& name,
                                       const std::string& display) {
        if (name == "EOF")                  return TokenKind::EndOfFile;
        if (name == "ERROR")                return TokenKind::Error;
        if (name.rfind("KW_", 0) == 0)      return TokenKind::Keyword;
        if (name.rfind("ID", 0) == 0)       return TokenKind::Identifier;
        if (name.rfind("NUM", 0) == 0)      return TokenKind::Number;
        if (name.rfind("STR", 0) == 0)      return TokenKind::String;
        if (name.rfind("SYM_", 0) == 0 ||
            name.rfind("OP_", 0) == 0) {
            // 单字符界符归为 Punct，其余归为 Operator（启发式）
            static constexpr std::string_view kPunctChars = "();,[]{}";
            if (display.size() == 1 &&
                kPunctChars.find(display) != std::string_view::npos) {
                return TokenKind::Punct;
            }
            return TokenKind::Operator;
        }
        return TokenKind::Unknown;
    }

    std::map<int, TokenType>   byCode_;    // code → 类型
    std::map<std::string, int> byName_;    // name → code
    int                        nextCode_ = 1;
};

} // namespace compiler
