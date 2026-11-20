# 任务二：LR 驱动 / 语法树构建 / 错误恢复。
# 默认不参与构建（顶层 CMake 由 -DCOMPILER_ENABLE_TASK2=ON 开启）。
# T2.4 起由 INTERFACE 占位转为 STATIC 实现。
# T2.4：LR(0) 项目集族（Lr0Automaton）。依赖 compiler_core（Grammar/Symbol）与
#       compiler_grammar（T2.5 起用 FirstFollow；模块依赖方向 app → parser → grammar → core）。
# T2.5：LALR(1)（Lalr1Builder：LR(1) + 同心合并，消费 FirstFollow::firstOfString）。
# T2.6：LALR(1) 分析表（ParseTableBuilder：ACTION/GOTO + 冲突报告，消费 Lalr1Data；
#       决策点 6：冲突一律报告）。
# T2.8：LR 驱动 + ParseTree 构建（LrDriver：消费 Grammar + ParseTableData + token 流；
#       决策点 4 进程内、决策点 5 B 遇错即停）。
add_library(compiler_parser STATIC
    Lr0Automaton.h
    Lr0Automaton.cpp
    Lalr1Builder.h
    Lalr1Builder.cpp
    ParseTable.h
    ParseTable.cpp
    LrDriver.h
    LrDriver.cpp
)

target_include_directories(compiler_parser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(compiler_parser PUBLIC compiler_core compiler_grammar)
