#pragma once

#include <QDockWidget>

#include "Diagnostic.h"

class QPlainTextEdit;

namespace compiler::gui {

// 日志面板：各任务步骤/诊断信息统一汇聚到此处（ARCHITECTURE.md §3/§7）
class LogDock : public QDockWidget {
public:
    explicit LogDock(QWidget* parent = nullptr);

    void log(const QString& message);
    // T1.9：统一 [severity] [stage] 格式输出一条诊断
    void logDiagnostic(const Diagnostic& diagnostic);
    void clearLog();

private:
    QPlainTextEdit* text_ = nullptr;
};

}
