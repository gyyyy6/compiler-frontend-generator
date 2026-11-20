#include "LexerCompilerRunner.h"

#include <QByteArray>
#include <QFile>
#include <QProcess>

namespace compiler::gui {

QString LexerCompilerRunner::gxxPath() const
{
    // ① 显式配置
    if (!gxxPathOverride_.isEmpty()) {
        return gxxPathOverride_;
    }
    // ② 环境变量 COMPILER_GXX
    const QByteArray env = qgetenv("COMPILER_GXX");
    if (!env.isEmpty()) {
        return QString::fromLocal8Bit(env);
    }
    // ③ 候选路径
    static const QStringList kCandidates = {
        QStringLiteral("C:/Qt/Tools/mingw1310_64/bin/g++.exe"),
        QStringLiteral("E:/QTCreator/Tools/mingw1310_64/bin/g++.exe"),
    };
    for (const QString& c : kCandidates) {
        if (QFile::exists(c)) {
            return c;
        }
    }
    // ④ PATH 中的 g++（由 QProcess 解析）
    return QStringLiteral("g++");
}

bool LexerCompilerRunner::compileScanner(const QString& sourceFile,
                                         const QString& outputExe,
                                         QString* errorMessage)
{
    const QString gxx = gxxPath();
    if (gxx.isEmpty()) {
        if (errorMessage) {
            *errorMessage =
                QStringLiteral("找不到 g++（请通过显式配置或 COMPILER_GXX 指定）");
        }
        return false;
    }

    QStringList args;
    args << QStringLiteral("-std=c++17") << QStringLiteral("-O2")
         << sourceFile << QStringLiteral("-o") << outputExe;

    // 阻塞等待：T1.9 可移至工作线程避免冻结 UI
    QProcess proc;
    proc.setProgram(gxx);
    proc.setArguments(args);
    proc.start();
    if (!proc.waitForStarted(10000)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法启动 g++（%1）：%2")
                                .arg(gxx, proc.errorString());
        }
        return false;
    }
    proc.waitForFinished(-1);
    const QByteArray err = proc.readAllStandardError();
    const bool ok = (proc.exitStatus() == QProcess::NormalExit) && proc.exitCode() == 0;
    if (!ok) {
        if (errorMessage) {
            *errorMessage = QString::fromLocal8Bit(err);
            if (errorMessage->isEmpty()) {
                *errorMessage =
                    QStringLiteral("编译失败，退出码 %1").arg(proc.exitCode());
            }
        }
        return false;
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool LexerCompilerRunner::runScanner(const QString& exe,
                                     const QStringList& args,
                                     QString* stdOut,
                                     QString* stdErr,
                                     int* exitCode)
{
    if (stdOut) stdOut->clear();
    if (stdErr) stdErr->clear();
    if (exitCode) *exitCode = -1;

    QProcess proc;
    proc.setProgram(exe);
    proc.setArguments(args);
    proc.start();
    if (!proc.waitForStarted(10000)) {
        if (stdErr) {
            *stdErr = QStringLiteral("无法启动扫描器（%1）：%2")
                          .arg(exe, proc.errorString());
        }
        return false;
    }
    proc.waitForFinished(-1);
    if (stdOut) {
        *stdOut = QString::fromLocal8Bit(proc.readAllStandardOutput());
    }
    if (stdErr) {
        *stdErr = QString::fromLocal8Bit(proc.readAllStandardError());
    }
    const int code =
        (proc.exitStatus() == QProcess::NormalExit) ? proc.exitCode() : -1;
    if (exitCode) *exitCode = code;
    return code == 0;
}

} // namespace compiler::gui
