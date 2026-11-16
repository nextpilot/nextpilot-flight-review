#pragma once

#include <QList>
#include <QString>

class QDockWidget;
class QMainWindow;
class QMenu;
class QToolButton;
class QWidget;

namespace ps::components
{
class Registry;
}

namespace ps::commands
{
class CommandManager;
}

// 停靠区生命周期：从贡献创建 Dock，负责标题栏、停靠、最大化、自动隐藏和布局状态。
class DockManager
{
public:
    explicit DockManager(QMainWindow *window);

    void createFromRegistry(const ps::components::Registry &registry);
    void registerCommand(ps::commands::CommandManager &commands);
    void darkenExisting();
    void repositionHandles();

    QWidget *createTitleBar(QDockWidget *dock);
    void toArea(QDockWidget *dock, Qt::DockWidgetArea area);
    void collapse(QDockWidget *dock);
    void showMenu(QToolButton *button, QMenu *menu);
    void toggleMax(QDockWidget *dock);
    void restoreMax();
    void toggleById(const QString &id);

private:
    QMainWindow *m_window = nullptr;
    QDockWidget *m_maxDock = nullptr;
    QWidget *m_maxCentral = nullptr;
    QList<QDockWidget *> m_maxHidden;
};
