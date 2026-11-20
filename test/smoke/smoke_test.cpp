// T1.1 冒烟测试：验证 LexerCompilerRunner（T1.8 起位于 src/gui）可链接可用。
#include "LexerCompilerRunner.h"

#include <iostream>

int main()
{
    compiler::gui::LexerCompilerRunner runner;

    QString error;
    // 传入不存在的源文件：无论本机 g++ 是否可用，都应失败并给出非空错误信息。
    const bool ok = runner.compileScanner(
        QStringLiteral("__no_such_scanner__.cpp"),
        QStringLiteral("__no_such_scanner__.exe"), &error);

    if (ok || error.isEmpty()) {
        std::cerr << "FAIL: compileScanner 对不存在的源文件应失败且给出错误信息" << std::endl;
        return 1;
    }

    std::cout << error.toStdString() << std::endl;
    std::cout << "skeleton smoke OK" << std::endl;
    return 0;
}
