# 任务二：.bnf 解析（BnfParser，T2.1）、文法化简（GrammarSimplifier，T2.2）、
#         First/Follow/Nullable（FirstFollow，T2.3）。
# 依赖仅 compiler_core；后续 LR(0) / LALR(1) / 分析表 追加到本模块。
add_library(compiler_grammar STATIC
    BnfParser.h
    BnfParser.cpp
    GrammarSimplifier.h
    GrammarSimplifier.cpp
    FirstFollow.h
    FirstFollow.cpp
)

target_include_directories(compiler_grammar PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(compiler_grammar PUBLIC compiler_core)
