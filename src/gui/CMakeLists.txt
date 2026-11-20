add_library(compiler_gui STATIC
    MainWindow.h
    MainWindow.cpp
    LogDock.h
    LogDock.cpp
    Task1Page.h
    Task1Page.cpp
    Task2Page.h
    Task2Page.cpp
    Task3Page.h
    Task3Page.cpp
    # T1.8：LexerCompilerRunner（QProcess 编译运行生成扫描器）由 src/core 迁入
    LexerCompilerRunner.h
    LexerCompilerRunner.cpp
    # T1.9：流水线工作线程 + 编译后端适配 + NFA/DFA 表格工具
    CompilerRunnerBackend.h
    CompilerRunnerBackend.cpp
    Task1PipelineWorker.h
    Task1PipelineWorker.cpp
    AutomatonTableView.h
    AutomatonTableView.cpp
)

# T2.10：任务二流水线工作线程 + LR 表格/语法树视图（依赖任务二算法库；
#        Task2Page 重写后依赖这些头文件，故 COMPILER_ENABLE_TASK2 须为 ON——项目默认）
# T3.5：任务三联合测试工作线程（JointPipeline + 报告/覆盖渲染）同样依赖
#        JointPipeline，放在同一开关内。
if(COMPILER_ENABLE_TASK2)
    target_sources(compiler_gui PRIVATE
        Task2PipelineWorker.h
        Task2PipelineWorker.cpp
        LrTableView.h
        LrTableView.cpp
        ParseTreeView.h
        ParseTreeView.cpp
        Task3PipelineWorker.h
        Task3PipelineWorker.cpp
    )
endif()

# 使 #include "MainWindow.h" / "LogDock.h" / "Task*Page.h" 可用
target_include_directories(compiler_gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(compiler_gui PUBLIC Qt6::Widgets compiler_core compiler_pipeline)
