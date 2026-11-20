add_executable(skeleton_smoke smoke/smoke_test.cpp)

# LexerCompilerRunner 已由 src/core 迁至 src/gui；smoke 链接 compiler_gui
#（传递 Qt6::Widgets 与 include 路径）
target_link_libraries(skeleton_smoke PRIVATE compiler_gui)

add_test(NAME skeleton_smoke COMMAND skeleton_smoke)

# T1.9：LexerPipeline 零 Qt 单测（fake 编译后端；g++ 独立编译运行，不依赖 Qt DLL）
add_executable(lexer_pipeline_test pipeline/lexer_pipeline_test.cpp)
# 零 Qt：无需 AUTOMOC（顶层全局 AUTOMOC ON；此目标显式关闭）
set_target_properties(lexer_pipeline_test PROPERTIES AUTOMOC OFF)
target_link_libraries(lexer_pipeline_test PRIVATE compiler_pipeline)

add_test(NAME lexer_pipeline_test COMMAND lexer_pipeline_test)

# T2.10：ParserPipeline 零 Qt 单测（主 A 路径：进程内扫描→sample.lex 文本→ParserPipeline
#        全链路；锚点 TINY 59 态/0 冲突/41 quads、Mini-C 128 态/1 个 S/R/139 quads；
#        降级路径 + 坏 .bnf 失败路径；需扫描 test.<lang>，故链 compiler_lexer）
if(COMPILER_ENABLE_TASK2)
    add_executable(parser_pipeline_test pipeline/parser_pipeline_test.cpp)
    set_target_properties(parser_pipeline_test PROPERTIES AUTOMOC OFF)
    target_link_libraries(parser_pipeline_test PRIVATE compiler_pipeline compiler_lexer)
    add_test(NAME parser_pipeline_test
             COMMAND parser_pipeline_test ${CMAKE_CURRENT_SOURCE_DIR}/languages)
endif()

# T3.4：JointPipeline 零 Qt 联合编排单测（fake CompileRunBackend；严格 runProducedLex 校验：
#        编译/运行失败回退预览 → joint-strict 且语法流水线零调用；失效/单步/诊断合并；
#        需扫描 test.minic 造 sample.lex 文本，故链 compiler_lexer）
if(COMPILER_ENABLE_TASK2)
    add_executable(joint_pipeline_test pipeline/joint_pipeline_test.cpp)
    set_target_properties(joint_pipeline_test PROPERTIES AUTOMOC OFF)
    target_link_libraries(joint_pipeline_test PRIVATE compiler_pipeline compiler_lexer)
    add_test(NAME joint_pipeline_test
             COMMAND joint_pipeline_test ${CMAKE_CURRENT_SOURCE_DIR}/languages)
endif()

# T2.1：BnfParser 零 Qt 单测（备 B 路径：重解析 .rules 提供终结符→tokenCode 映射）
if(COMPILER_ENABLE_TASK2)
    add_executable(bnf_parser_test grammar/bnf_parser_test.cpp)
    set_target_properties(bnf_parser_test PROPERTIES AUTOMOC OFF)
    target_link_libraries(bnf_parser_test PRIVATE compiler_grammar compiler_lexer)
    # 语言包根目录作为运行时参数（ctest 工作目录为 build/test，传源码树绝对路径）
    add_test(NAME bnf_parser_test
             COMMAND bnf_parser_test ${CMAKE_CURRENT_SOURCE_DIR}/languages)
endif()

# T2.2：GrammarSimplifier 零 Qt 单测（文法化简：无用/不可达删除 + 重编号 + 动作日志）
if(COMPILER_ENABLE_TASK2)
    add_executable(grammar_simplifier_test grammar/grammar_simplifier_test.cpp)
    set_target_properties(grammar_simplifier_test PROPERTIES AUTOMOC OFF)
    target_link_libraries(grammar_simplifier_test PRIVATE compiler_grammar compiler_lexer)
    add_test(NAME grammar_simplifier_test
             COMMAND grammar_simplifier_test ${CMAKE_CURRENT_SOURCE_DIR}/languages)
endif()

# T2.3：FirstFollow 零 Qt 单测（First/Follow/Nullable 不动点 + firstOfString ε 剥离）
# 仅依赖 grammar 模块（不需要终结符 registry，故不链 compiler_lexer）
if(COMPILER_ENABLE_TASK2)
    add_executable(first_follow_test grammar/first_follow_test.cpp)
    set_target_properties(first_follow_test PROPERTIES AUTOMOC OFF)
    target_link_libraries(first_follow_test PRIVATE compiler_grammar)
    add_test(NAME first_follow_test
             COMMAND first_follow_test ${CMAKE_CURRENT_SOURCE_DIR}/languages)
endif()

# T2.4：Lr0Automaton 零 Qt 单测（LR(0) 项目集族：closure/goto/BFS + 增广 S'）
if(COMPILER_ENABLE_TASK2)
    add_executable(lr0_test parser/lr0_test.cpp)
    set_target_properties(lr0_test PROPERTIES AUTOMOC OFF)
    target_link_libraries(lr0_test PRIVATE compiler_parser compiler_grammar)
    add_test(NAME lr0_test
             COMMAND lr0_test ${CMAKE_CURRENT_SOURCE_DIR}/languages)
endif()

# T2.5：Lalr1Builder 零 Qt 单测（LALR(1)：LR(1) + 同心合并；状态数锚点/冲突/不变量）
if(COMPILER_ENABLE_TASK2)
    add_executable(lalr1_test parser/lalr1_test.cpp)
    set_target_properties(lalr1_test PROPERTIES AUTOMOC OFF)
    target_link_libraries(lalr1_test PRIVATE compiler_parser compiler_grammar)
    add_test(NAME lalr1_test
             COMMAND lalr1_test ${CMAKE_CURRENT_SOURCE_DIR}/languages)
endif()

# T2.6：ParseTableBuilder 零 Qt 单测（LALR(1) 分析表 ACTION/GOTO + 冲突报告；决策点 6）
if(COMPILER_ENABLE_TASK2)
    add_executable(parse_table_test parser/parse_table_test.cpp)
    set_target_properties(parse_table_test PROPERTIES AUTOMOC OFF)
    target_link_libraries(parse_table_test PRIVATE compiler_parser compiler_grammar)
    add_test(NAME parse_table_test
             COMMAND parse_table_test ${CMAKE_CURRENT_SOURCE_DIR}/languages)
endif()

# T2.8：LrDriver 零 Qt 单测（LR 驱动 + ParseTree；决策点 4 进程内 / 决策点 5 B 遇错即停；
#       需扫描 test.<lang>，故链 compiler_lexer）
if(COMPILER_ENABLE_TASK2)
    add_executable(lr_driver_test parser/lr_driver_test.cpp)
    set_target_properties(lr_driver_test PROPERTIES AUTOMOC OFF)
    target_link_libraries(lr_driver_test PRIVATE compiler_parser compiler_grammar compiler_lexer)
    add_test(NAME lr_driver_test
             COMMAND lr_driver_test ${CMAKE_CURRENT_SOURCE_DIR}/languages)
endif()

# T2.9：IrGenerator 零 Qt 单测（ParseTree → 四元式 IR；锚点 TINY 41 / Mini-C 75 /
#       手工例 n+n*n 2 quads / 前后缀自增语义；需扫描 test.<lang> 并走 LrDriver，
#       故链 compiler_lexer；compiler_ir 已 PUBLIC 依赖 parser/grammar/core）
if(COMPILER_ENABLE_TASK2)
    add_executable(ir_generator_test ir/ir_generator_test.cpp)
    set_target_properties(ir_generator_test PROPERTIES AUTOMOC OFF)
    target_link_libraries(ir_generator_test PRIVATE compiler_ir compiler_parser
                                                   compiler_grammar compiler_lexer)
    add_test(NAME ir_generator_test
             COMMAND ir_generator_test ${CMAKE_CURRENT_SOURCE_DIR}/languages)
endif()

# T2.7：SampleLexReader 零 Qt 单测（sample.lex 读取 + T1→T2 链路往返；只依赖 T1 模块）
add_executable(sample_lex_reader_test lexer/sample_lex_reader_test.cpp)
set_target_properties(sample_lex_reader_test PROPERTIES AUTOMOC OFF)
target_link_libraries(sample_lex_reader_test PRIVATE compiler_lexer)
add_test(NAME sample_lex_reader_test
         COMMAND sample_lex_reader_test ${CMAKE_CURRENT_SOURCE_DIR}/languages)
