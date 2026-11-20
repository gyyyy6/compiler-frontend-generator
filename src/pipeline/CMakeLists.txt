# 任务一/二流水线编排（LexerPipeline T1.9 / ParserPipeline T2.10，均零 Qt）。
# 任务一依赖：core/regex/lexer/codegen；编译/运行经 CompileRunBackend 抽象注入，
# Qt 适配器（CompilerRunnerBackend）位于 src/gui。
# T2.10：ParserPipeline 依赖 grammar/parser/ir（由 COMPILER_ENABLE_TASK2 控制）。
add_library(compiler_pipeline STATIC
    LexerPipeline.h
    LexerPipeline.cpp
)

# 零 Qt：显式关闭 AUTOMOC（纯 C++17/STL，无 Q_OBJECT，避免 moc_predefs 子进程开销）
set_target_properties(compiler_pipeline PROPERTIES AUTOMOC OFF)

target_include_directories(compiler_pipeline PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(compiler_pipeline PUBLIC
    compiler_core
    compiler_regex
    compiler_lexer
    compiler_codegen
)

# T2.10：语法分析流水线（ParserPipeline，零 Qt，进程内无 CompileRunBackend）。
# T3.4：联合流水线（JointPipeline，严格串联 LexerPipeline → ParserPipeline）。
# 关闭 COMPILER_ENABLE_TASK2 时保持 T1 仅构建能力。
if(COMPILER_ENABLE_TASK2)
    target_sources(compiler_pipeline PRIVATE
        ParserPipeline.h
        ParserPipeline.cpp
        JointPipeline.h
        JointPipeline.cpp
    )
    target_link_libraries(compiler_pipeline PUBLIC
        compiler_grammar
        compiler_parser
        compiler_ir
    )
endif()
