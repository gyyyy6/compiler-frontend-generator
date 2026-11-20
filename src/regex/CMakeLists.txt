# 任务一：正则解析（T1.3）/ Thompson 构造（T1.3）/ 子集构造（T1.4）/
# Hopcroft 最小化（T1.5）。
add_library(compiler_regex STATIC
    RegexAst.h
    RegexParser.h
    RegexParser.cpp
    RegexToNfa.h
    RegexToNfa.cpp
    NfaToDfa.h
    NfaToDfa.cpp
    MinimizeDfa.h
    MinimizeDfa.cpp
)

target_include_directories(compiler_regex PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(compiler_regex PUBLIC compiler_core)
