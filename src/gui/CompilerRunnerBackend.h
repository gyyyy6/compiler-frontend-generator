#pragma once

// LexerCompilerRunner 的 pipeline 适配器（T1.9）
// 实现 pipeline::CompileRunBackend：把零 Qt 接口翻译成 LexerCompilerRunner 的
// QString 接口。LexerCompilerRunner 本体（T1.8）零修改。

#include <string>
#include <vector>

#include "LexerPipeline.h"

namespace compiler::gui {

class CompilerRunnerBackend : public pipeline::CompileRunBackend {
public:
    bool compile(const std::string& sourceFile,
                 const std::string& outputExe,
                 std::string& errorOut) override;

    bool run(const std::string& exe,
             const std::vector<std::string>& args,
             std::string& stdOut,
             std::string& stdErr,
             int& exitCode) override;
};

} // namespace compiler::gui
