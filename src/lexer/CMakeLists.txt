# 任务一 T1.6/T1.7：.rules 解析 / LexerSpec / LexerBuilder / Scanner（表驱动扫描器）。
# T1.1-T1.5 骨架期无源文件，用 INTERFACE 库占位；T1.6 起改为 STATIC。
add_library(compiler_lexer STATIC
    LexerSpec.h
    LexRulesParser.h
    LexRulesParser.cpp
    LexerBuilder.h
    LexerBuilder.cpp
    Scanner.h
    Scanner.cpp
    # T1.8：sample.lex 渲染器（渲染单点化，codegen 复用）
    SampleLexWriter.h
    SampleLexWriter.cpp
    # T2.7：sample.lex 读取器（与 SampleLexWriter 对称；T1→T2 输入链路）
    SampleLexReader.h
    SampleLexReader.cpp
)

target_include_directories(compiler_lexer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(compiler_lexer PUBLIC compiler_core compiler_regex)
