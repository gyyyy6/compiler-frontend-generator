# 任务一 T1.8：最小 DFA → 独立 C++ 扫描器源码生成（方法二，见 docs/PROGRESS.md「T1.8」）。
# 渲染单点化：头部/渲染模式契约来自 SampleLexWriter（lexer 模块）。
add_library(compiler_codegen STATIC
    ScannerSourceGenerator.h
    ScannerSourceGenerator.cpp
)

target_include_directories(compiler_codegen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(compiler_codegen PUBLIC compiler_core compiler_lexer)

# 模板目录编译定义：生成器运行期定位 templates/scanner_main.cpp.in。
# 正斜杠路径避免 C 字符串转义问题（Windows 反斜杠会吞掉 \t 等）。
string(REPLACE "\\" "/" _codegen_templates "${CMAKE_CURRENT_SOURCE_DIR}/templates")
target_compile_definitions(compiler_codegen PRIVATE
    "COMPILER_CODEGEN_TEMPLATE_DIR=\"${_codegen_templates}\"")
