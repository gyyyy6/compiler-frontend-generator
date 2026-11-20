# 任务二：中间代码生成（T2.9，选做转必做）。
# ParseTree → 四元式 IR（IrGenerator），op 白名单/临时变量/标签/回填。
# 依赖方向（ARCHITECTURE.md §4）：app → ir → parser → grammar → core。
add_library(compiler_ir STATIC
    IrGenerator.h
    IrGenerator.cpp
)

target_include_directories(compiler_ir PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(compiler_ir PUBLIC compiler_parser compiler_grammar compiler_core)
