#include "MainWindow.h"

#include <QAction>
#include <QDockWidget>
#include <QFileDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTabWidget>

#include "LogDock.h"
#include "Task1Page.h"
#include "Task2Page.h"
#include "Task3Page.h"

namespace compiler::gui {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("编译原理课程设计 · CompilerCourseApp"));
    resize(1100, 720);

    createLogDock();       // 先建日志 Dock，供 Task1Page 信号接线
    createCentralTabs();
    createMenus();

    statusBar()->showMessage(tr("就绪 · 任务一 / 二 / 三均可运行"));
    logDock_->log(tr("主窗口初始化完成（T3.5 联合测试页面已启用）"));
}

void MainWindow::createCentralTabs()
{
    mainTabs_ = new QTabWidget(this);
    mainTabs_->setDocumentMode(true);

    auto* task1Page = new Task1Page(mainTabs_);
    // T1.9：Task1Page 日志/诊断/状态转发到 LogDock 与 StatusBar
    connect(task1Page, &Task1Page::logMessage, logDock_, &LogDock::log);
    connect(task1Page, &Task1Page::diagnostic, logDock_, &LogDock::logDiagnostic);
    connect(task1Page, &Task1Page::statusMessage, this,
            [this](const QString& msg) { statusBar()->showMessage(msg); });

    auto* task2Page = new Task2Page(mainTabs_);
    // T2.10：Task2Page 日志/诊断/状态转发到 LogDock 与 StatusBar
    connect(task2Page, &Task2Page::logMessage, logDock_, &LogDock::log);
    connect(task2Page, &Task2Page::diagnostic, logDock_, &LogDock::logDiagnostic);
    connect(task2Page, &Task2Page::statusMessage, this,
            [this](const QString& msg) { statusBar()->showMessage(msg); });

    auto* task3Page = new Task3Page(mainTabs_);
    // T3.5：任务三日志/诊断/状态转发 + 跳转任务一/二（不复制其内部状态）
    connect(task3Page, &Task3Page::logMessage, logDock_, &LogDock::log);
    connect(task3Page, &Task3Page::diagnostic, logDock_, &LogDock::logDiagnostic);
    connect(task3Page, &Task3Page::statusMessage, this,
            [this](const QString& msg) { statusBar()->showMessage(msg); });
    connect(task3Page, &Task3Page::requestTask1, this, &MainWindow::goToTask1);
    connect(task3Page, &Task3Page::requestTask2, this, &MainWindow::goToTask2);

    mainTabs_->addTab(task1Page, tr("任务一 · 词法分析"));
    mainTabs_->addTab(task2Page, tr("任务二 · 语法分析"));
    mainTabs_->addTab(task3Page, tr("任务三 · Mini-C 测试"));

    setCentralWidget(mainTabs_);
}

void MainWindow::goToTask1()
{
    mainTabs_->setCurrentIndex(0);
    statusBar()->showMessage(tr("已切换到任务一 · 词法分析"));
}

void MainWindow::goToTask2()
{
    mainTabs_->setCurrentIndex(1);
    statusBar()->showMessage(tr("已切换到任务二 · 语法分析"));
}

void MainWindow::createLogDock()
{
    logDock_ = new LogDock(this);
    addDockWidget(Qt::BottomDockWidgetArea, logDock_);
}

void MainWindow::createMenus()
{
    // ---- 文件 ----
    QMenu* fileMenu = menuBar()->addMenu(tr("文件(&F)"));

    QAction* openProjectAction = fileMenu->addAction(tr("打开语言项目(&O)..."));
    openProjectAction->setShortcut(QKeySequence::Open);
    connect(openProjectAction, &QAction::triggered, this, &MainWindow::openLanguageProject);

    fileMenu->addSeparator();

    QAction* quitAction = fileMenu->addAction(tr("退出(&Q)"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &MainWindow::close);

    // ---- 任务 ----
    QMenu* taskMenu = menuBar()->addMenu(tr("任务(&T)"));

    const auto addTaskAction = [this, taskMenu](const QString& text, int index) {
        QAction* act = taskMenu->addAction(text);
        act->setData(index);
        connect(act, &QAction::triggered, this, [this, index]() {
            mainTabs_->setCurrentIndex(index);
        });
    };
    addTaskAction(tr("任务一 · 词法分析(&1)"), 0);
    addTaskAction(tr("任务二 · 语法分析(&2)"), 1);
    addTaskAction(tr("任务三 · Mini-C 测试(&3)"), 2);

    // ---- 工具 ----
    QMenu* toolMenu = menuBar()->addMenu(tr("工具(&U)"));

    QAction* settingsAction = toolMenu->addAction(tr("设置(&S)..."));
    settingsAction->setEnabled(false);
    settingsAction->setToolTip(tr("任务一后续版本实现"));

    // ---- 视图 ----
    QMenu* viewMenu = menuBar()->addMenu(tr("视图(&V)"));

    viewLogAction_ = new QAction(tr("日志面板(&L)"), this);
    viewLogAction_->setCheckable(true);
    viewLogAction_->setChecked(true);
    connect(viewLogAction_, &QAction::toggled, logDock_, &QDockWidget::setVisible);
    viewMenu->addAction(viewLogAction_);

    // ---- 帮助 ----
    QMenu* helpMenu = menuBar()->addMenu(tr("帮助(&H)"));

    QAction* aboutAction = helpMenu->addAction(tr("关于(&A)"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::about);
}

void MainWindow::openLanguageProject()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("打开语言项目"), QString(), QFileDialog::ShowDirsOnly);
    if (dir.isEmpty())
        return;

    logDock_->log(tr("已选择语言项目目录：%1（加载逻辑将在 T1.6 实现）").arg(dir));
    statusBar()->showMessage(tr("语言项目：%1").arg(dir));
}

void MainWindow::about()
{
    QMessageBox::about(this, tr("关于"),
        tr("<b>编译原理课程设计 · CompilerCourseApp</b><br/>"
           "版本 0.3.0（T3.5 任务三联合测试）<br/><br/>"
           "任务一 · 词法分析（正则→NFA→DFA→最小化→生成扫描器→sample.lex）<br/>"
           "任务二 · 语法分析（BNF→LALR(1)→语法树→四元式 IR）<br/>"
           "任务三 · Mini-C 测试（联合流水线：真实扫描器 → 语法树 → IR → 报告）"));
}

} // namespace compiler::gui
