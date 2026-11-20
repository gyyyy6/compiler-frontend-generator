#pragma once

// 编译运行器（T1.8，由 src/core 迁至 src/gui 并补全）
// 使用 QProcess 调用本地 MinGW g++ 编译并运行生成的扫描器。
// 仅由宿主 GUI / app 使用，不进入生成扫描器（生成代码零 Qt 依赖）。
//
// g++ 定位优先级（T1.8 v2）：
//   ①显式配置（setGxxPath）→ ②环境变量 COMPILER_GXX → ③候选路径
//     （C:\Qt\Tools\mingw1310_64\bin\g++.exe、E:\QTCreator\Tools\mingw1310_64\bin\g++.exe）
//     → ④PATH 中的 g++。

#include <QString>
#include <QStringList>

namespace compiler::gui {

class LexerCompilerRunner {
public:
    LexerCompilerRunner() = default;

    // 显式指定 g++ 路径（优先于环境变量/候选/PATH）
    void setGxxPath(const QString& path) { gxxPathOverride_ = path; }

    // 按优先级解析 g++ 路径（无显式配置且无候选时回退 "g++"，由 QProcess 走 PATH）
    QString gxxPath() const;

    // 编译 scanner.cpp → 可执行文件（如 tiny_scanner）。成功返回 true。
    bool compileScanner(const QString& sourceFile,
                        const QString& outputExe,
                        QString* errorMessage = nullptr);

    // 运行扫描器：<exe> <args...>；捕获 stdout/stderr 与退出码。
    // 退出码为 0 时返回 true。
    bool runScanner(const QString& exe,
                    const QStringList& args,
                    QString* stdOut,
                    QString* stdErr,
                    int* exitCode);

private:
    QString gxxPathOverride_;   // 显式配置
};

} // namespace compiler::gui
