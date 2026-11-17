#include "ModuleContext.h"

#include "MainWindow.h"
#include "CommandManager.h"
#include "LogManager.h"
#include "SessionManager.h"
#include "DockManager.h"
#include "PropertyManager.h"

#include <QHash>
#include <QScreen>
#include <QStatusBar>
#include <QTabWidget>
#include <QDockWidget>

namespace
{
QHash<QWidget *, ps::runtime::ModuleContext *> &contexts()
{
    static QHash<QWidget *, ps::runtime::ModuleContext *> map;
    return map;
}
} // namespace

namespace ps::runtime
{

ModuleContext::ModuleContext(QMainWindow *host, AppSession &session)
    : m_host(host), m_session(&session)
{
}

ModuleContext::~ModuleContext() = default;

QWidget *ModuleContext::window() const
{
    return static_cast<QWidget *>(m_host);
}

QTabWidget *ModuleContext::pages() const
{
    return static_cast<MainWindow *>(m_host)->pages();
}

void ModuleContext::setPageHost(QTabWidget *pages)
{
    static_cast<MainWindow *>(m_host)->setPageHost(pages);
}

ps::commands::CommandManager &ModuleContext::commands() const
{
    return static_cast<MainWindow *>(m_host)->commands();
}

LogManager &ModuleContext::logs() const
{
    return static_cast<MainWindow *>(m_host)->logs();
}

ps::shell::PropertyManager &ModuleContext::properties() const
{
    return static_cast<MainWindow *>(m_host)->properties();
}

DockManager &ModuleContext::docks() const
{
    return static_cast<MainWindow *>(m_host)->docks();
}

SessionManager &ModuleContext::session() const
{
    return static_cast<MainWindow *>(m_host)->session();
}

void ModuleContext::addDockWidget(Qt::DockWidgetArea area, QDockWidget *dock)
{
    m_host->addDockWidget(area, dock);
}

void ModuleContext::resizeDocks(const QList<QDockWidget *> &docks, const QList<int> &sizes,
                                Qt::Orientation orientation)
{
    m_host->resizeDocks(docks, sizes, orientation);
}

QDockWidget *ModuleContext::findDock(const QString &id) const
{
    return m_host->findChild<QDockWidget *>(id);
}

QScreen *ModuleContext::screen() const
{
    return window()->screen();
}

void ModuleContext::showStatus(const QString &text)
{
    if (auto *bar = m_host->statusBar())
        bar->showMessage(text);
}

void ModuleContext::installEventFilter(QObject *filter)
{
    m_host->installEventFilter(filter);
}

ServiceRegistry &ModuleContext::services() const
{
    return m_session->services();
}

ModuleContext *ModuleContext::fromWidget(QWidget *widget)
{
    if (!widget)
        return nullptr;
    return contexts().value(widget->window());
}

void ModuleContext::attach(QWidget *window, ModuleContext *context)
{
    if (window && context)
        contexts().insert(window, context);
}

void ModuleContext::detach(QWidget *window)
{
    if (window)
        contexts().remove(window);
}

} // namespace ps::runtime
