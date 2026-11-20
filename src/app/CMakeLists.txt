add_executable(CompilerCourseApp
    main.cpp
)

target_link_libraries(CompilerCourseApp PRIVATE
    compiler_gui
    compiler_lexer      # --lex CLI 分支（rules 解析/DFA 构建/扫描/渲染）
    Qt6::Widgets
)

# T2.10：--parse CLI 分支依赖 ParserPipeline（经 compiler_pipeline 传递 grammar/parser/ir；
#        main.cpp 用 COMPILER_ENABLE_TASK2 宏守卫）
# T3.5：--minic CLI 分支额外复用 compiler_gui 的 CompilerRunnerBackend（QProcess）
#        与 Task3PipelineWorker（报告/覆盖渲染），头文件经 compiler_gui 的 PUBLIC
#        include 目录传递；无需新增链接项（Qt6::Widgets 已传递 Qt6::Core）。
if(COMPILER_ENABLE_TASK2)
    target_compile_definitions(CompilerCourseApp PRIVATE COMPILER_ENABLE_TASK2=1)
    target_link_libraries(CompilerCourseApp PRIVATE compiler_pipeline)
endif()
