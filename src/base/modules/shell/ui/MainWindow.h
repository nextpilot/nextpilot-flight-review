#pragma once

#include <QMainWindow>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QPair>

#include "Component.h"
#include "ExtensionHost.h"
#include "CommandRegistry.h"

class QDockWidget;
class QTabWidget;
class QPlainTextEdit;
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
    void attachLogView(QPlainTextEdit *view);
    QWidget *createDockTitleBar(QDockWidget *dock);
    void dockWidgetToArea(QDockWidget *dock, Qt::DockWidgetArea area);
    void collapseDockWidget(QDockWidget *dock);
    void showDockMenu(QToolButton *btn, QMenu *menu);
    void toggleDockMax(QDockWidget *dock);
    void restoreDockMax();

    ps::commands::CommandRegistry &commands() { return m_commands; }
    ps::components::ExtensionHost &extensions() { return m_extensions; }
    QTabWidget *pages() const { return m_pages; }
    void setPageHost(QTabWidget *p) {
        m_pages = p;
        if (p)
            setCentralWidget(p);
    }
    void appendLog(const QString &text) { log(text); }
    QString kernelInfo() const { return m_kernelInfo; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    QWidget *createRibbon();
    void registerCommands();
    void createDocks();
    void log(const QString &text);

    QTabWidget *m_pages = nullptr;
    QDockWidget *m_maxDock = nullptr;
    QWidget *m_maxDockCentral = nullptr;
    QList<QDockWidget *> m_maxDockHidden;
    QPlainTextEdit *m_logView = nullptr;
    QString m_kernelInfo;
    ps::components::ExtensionHost m_extensions;
    ps::commands::CommandRegistry m_commands;
};
