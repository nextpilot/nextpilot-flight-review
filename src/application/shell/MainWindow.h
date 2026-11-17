#pragma once

#include <QMainWindow>
#include <QString>

#include "ExtensionManager.h"
#include "CommandManager.h"
#include "SessionManager.h"
#include "LogManager.h"
#include "DockManager.h"
#include "RibbonManager.h"
#include "PropertyManager.h"
#include "ModuleContext.h"

class QTabWidget;

// 主窗口骨架：Ribbon + 可停靠区 + 页签容器。通用能力，不认识数据源/画布业务。
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    ps::commands::CommandManager &commands() { return m_commands; }
    ExtensionManager &extensions() { return m_extensions; }
    DockManager &docks() { return m_docks; }
    SessionManager &session() { return m_session; }
    QTabWidget *pages() const { return m_pages; }
    void setPageHost(QTabWidget *p) {
        m_pages = p;
        if (p)
            m_docks.setCentralContent(p);
    }
    LogManager &logs() { return m_logs; }
    ps::shell::PropertyManager &properties() { return m_props; }
    QString kernelInfo() const { return m_kernelInfo; }
    void refreshTitle();
    void applyTheme();

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
    ps::shell::PropertyManager m_props;
    RibbonManager m_ribbon;
    ps::runtime::AppSession m_appSession;
    ps::runtime::ModuleContext m_moduleContext;
};
