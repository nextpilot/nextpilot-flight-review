#pragma once

#include <QMainWindow>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QPair>

#include "Component.h"
#include "ExtensionManager.h"
#include "CommandManager.h"
#include "SessionManager.h"
#include "LogManager.h"
#include "DockManager.h"
#include "RibbonManager.h"

class QDockWidget;
class QTabWidget;
class QToolButton;
class QMenu;
class SARibbonBar;
class SARibbonPanel;

// 主窗口骨架：Ribbon + 可停靠区 + 页签容器。通用能力，不认识数据源/画布业务。
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    QString defaultLayoutPath() const;
    QWidget *createDockTitleBar(QDockWidget *dock);
    void dockWidgetToArea(QDockWidget *dock, Qt::DockWidgetArea area);
    void collapseDockWidget(QDockWidget *dock);
    void showDockMenu(QToolButton *btn, QMenu *menu);
    void toggleDockMax(QDockWidget *dock);
    void restoreDockMax();

    ps::commands::CommandManager &commands() { return m_commands; }
    ExtensionManager &extensions() { return m_extensions; }
    DockManager &docks() { return m_docks; }
    SessionManager &session() { return m_session; }
    QTabWidget *pages() const { return m_pages; }
    void setPageHost(QTabWidget *p) {
        m_pages = p;
        if (p)
            setCentralWidget(p);
    }
    void appendLog(const QString &text) { log(text); }
    QString kernelInfo() const { return m_kernelInfo; }
    void refreshTitle();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void log(const QString &text);

    QTabWidget *m_pages = nullptr;
    QString m_kernelInfo;
    ExtensionManager m_extensions;
    ps::commands::CommandManager m_commands;
    SessionManager m_session;
    LogManager m_logs;
    DockManager m_docks;
    RibbonManager m_ribbon;
};
