cmake_minimum_required(VERSION 3.16)

project(CompilerCourseDesign VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# 全局编译配置：C++17 + Qt6 AUTOMOC（ARCHITECTURE.md §10）
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# 统一输出目录：可执行文件 → build/app，库文件 → build/lib
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/app)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ---------------------------------------------------------------------------
# Qt6 Widgets（GUI 层使用；LexerCompilerRunner 位于 src/gui，core 保持零 Qt）
# ---------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Widgets)
message(STATUS "Using Qt ${QT_VERSION}")

# ---------------------------------------------------------------------------
# 模块（依赖方向：core ← regex ← lexer；codegen 依赖 core/lexer）
# ---------------------------------------------------------------------------
# 任务二开关必须在 src/pipeline 之前声明：pipeline 的 CMakeLists 用它决定是否
# 编入 ParserPipeline/JointPipeline。若声明在 add_subdirectory 之后，全新配置
# （缓存为空）时该变量尚未定义，pipeline 会静默跳过任务二源文件，等到编译 GUI
# 才报 "FirstFollow.h: No such file or directory"。
option(COMPILER_ENABLE_TASK2 "Enable task-2 libraries (grammar/parser)" ON)

add_subdirectory(src/core)
add_subdirectory(src/regex)
add_subdirectory(src/lexer)
add_subdirectory(src/codegen)
add_subdirectory(src/pipeline)

# 任务二：grammar/parser/ir（T2.1 起默认参与构建；T2.4 起 parser 由 INTERFACE 占位转
# STATIC 实现；T2.9 起新增 ir 模块 IrGenerator 中间代码生成）
# ARCHITECTURE.md §10：COMPILER_ENABLE_TASK2 默认 ON。
if(COMPILER_ENABLE_TASK2)
    add_subdirectory(src/grammar)
    add_subdirectory(src/parser)
    add_subdirectory(src/ir)
endif()

# UI 页面库（含 MainWindow）与可执行程序
add_subdirectory(src/gui)
add_subdirectory(src/app)

# 测试
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(test)
endif()
