#include "CompilerRunnerBackend.h"

#include <QString>
#include <QStringList>

#include "LexerCompilerRunner.h"

namespace compiler::gui {

bool CompilerRunnerBackend::compile(const std::string& sourceFile,
                                    const std::string& outputExe,
                                    std::string& errorOut)
{
    LexerCompilerRunner runner;
    QString err;
    const bool ok = runner.compileScanner(
        QString::fromUtf8(sourceFile.c_str(), static_cast<int>(sourceFile.size())),
        QString::fromUtf8(outputExe.c_str(), static_cast<int>(outputExe.size())),
        &err);
    if (!ok) {
        errorOut = err.toStdString();
    }
    return ok;
}

bool CompilerRunnerBackend::run(const std::string& exe,
                                const std::vector<std::string>& args,
                                std::string& stdOut,
                                std::string& stdErr,
                                int& exitCode)
{
    LexerCompilerRunner runner;
    QStringList qargs;
    for (const std::string& a : args) {
        qargs << QString::fromUtf8(a.c_str(), static_cast<int>(a.size()));
    }
    QString out;
    QString err;
    const bool ok = runner.runScanner(
        QString::fromUtf8(exe.c_str(), static_cast<int>(exe.size())),
        qargs, &out, &err, &exitCode);
    stdOut = out.toStdString();
    stdErr = err.toStdString();
    return ok;
}

} // namespace compiler::gui
