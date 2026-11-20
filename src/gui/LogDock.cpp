#include "LogDock.h"

#include <QDateTime>
#include <QPlainTextEdit>

namespace compiler::gui {

LogDock::LogDock(QWidget* parent)
    : QDockWidget(tr("日志"), parent)
{
    setObjectName(QStringLiteral("LogDock"));

    text_ = new QPlainTextEdit(this);
    text_->setReadOnly(true);
    text_->setMaximumBlockCount(5000);
    setWidget(text_);
}

void LogDock::log(const QString& message)
{
    const QString line =
        QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")) + QStringLiteral("  ") + message;
    text_->appendPlainText(line);
}

void LogDock::logDiagnostic(const Diagnostic& d)
{
    QString sev;
    switch (d.severity) {
    case Severity::Info:    sev = tr("Info");    break;
    case Severity::Warning: sev = tr("Warning"); break;
    case Severity::Error:   sev = tr("Error");   break;
    }
    QString msg = QStringLiteral("[%1] [%2] %3")
                      .arg(sev, QString::fromStdString(d.stage),
                           QString::fromStdString(d.message));
    if (d.loc.line >= 1) {
        msg += QStringLiteral(" (line %1)").arg(d.loc.line);
    }
    log(msg);
}

void LogDock::clearLog()
{
    text_->clear();
}

} // namespace compiler::gui
