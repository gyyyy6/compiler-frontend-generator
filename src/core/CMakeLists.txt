add_library(compiler_core STATIC
    # T1.2 统一数据结构（纯 C++17 + STL，零 Qt 依赖，见 DATA_STRUCTURES.md）
    Token.h
    TokenCodeRegistry.h
    Symbol.h
    Grammar.h
    NFA.h
    DFA.h
    ParseTree.h
    Diagnostic.h
    Result.h
)

# 使 #include "Token.h" 等平铺头文件可用
target_include_directories(compiler_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
