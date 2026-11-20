#pragma once

#include <QMainWindow>

class QAction;
class QTabWidget;

namespace compiler::gui {

class LogDock;
class Task1Page;
class Task2Page;
class Task3Page;

// 主窗口：菜单栏 + 三个任务 Tab + 日志 Dock + 状态栏（ARCHITECTURE.md §3/§7）
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void openLanguageProject();
    void goToTask1();   // 任务三「查看任务一词法中间产物」跳转
    void goToTask2();   // 任务三「查看任务二完整分析表」跳转
    void about();

private:
    void createCentralTabs();
    void createLogDock();
    void createMenus();

    QTabWidget* mainTabs_ = nullptr;
    LogDock* logDock_ = nullptr;
    QAction* viewLogAction_ = nullptr;
};

}