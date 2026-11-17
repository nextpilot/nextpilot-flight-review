#pragma once

#include "AppSession.h"

#include <QList>
#include <QString>
#include <Qt>

class QMainWindow;
class QWidget;
class QTabWidget;
class QDockWidget;
class QScreen;
class QObject;
class QStatusBar;
class LogManager;
class SessionManager;
class DockManager;

namespace ps::commands
{
class CommandManager;
}

namespace ps::shell
{
class PropertyManager;
}

namespace ps::runtime
{

// 模块拿到的宿主上下文。只暴露“能做什么”，不暴露 MainWindow 类型。
// 当前实现是 shell 适配层；后续把 commands/logs/settings 收口成接口时，
// 模块侧调用不变，只换内部实现。
struct ModuleContext
{
    explicit ModuleContext(QMainWindow *host, AppSession &session);
    ~ModuleContext();

    QWidget *window() const;
    QTabWidget *pages() const;
    void setPageHost(QTabWidget *pages);

    ps::commands::CommandManager &commands() const;
    LogManager &logs() const;
    ps::shell::PropertyManager &properties() const;
    DockManager &docks() const;
    SessionManager &session() const;

    void addDockWidget(Qt::DockWidgetArea area, QDockWidget *dock);
    void resizeDocks(const QList<QDockWidget *> &docks, const QList<int> &sizes,
                     Qt::Orientation orientation);
    QDockWidget *findDock(const QString &id) const;
    QScreen *screen() const;
    void showStatus(const QString &text);
    void installEventFilter(QObject *filter);

    ServiceRegistry &services() const;

    // Ribbon/面板回调只拿到 QWidget*，用它回找所属窗口的上下文。
    static ModuleContext *fromWidget(QWidget *widget);
    static void attach(QWidget *window, ModuleContext *context);
    static void detach(QWidget *window);

private:
    QMainWindow *m_host = nullptr;
    AppSession *m_session = nullptr;
};

} // namespace ps::runtime
