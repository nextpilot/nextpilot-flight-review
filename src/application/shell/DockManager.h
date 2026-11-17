#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

class QMainWindow;
class QWidget;

namespace ads
{
class CDockManager;
class CDockWidget;
}

namespace ps::components
{
class Registry;
}

namespace ps::commands
{
class CommandManager;
}

// 停靠区生命周期：ADS 承载拖拽/标签/浮动/自动隐藏，本类只做贡献注册与命令桥接。
class DockManager
{
public:
    explicit DockManager(QMainWindow *window);
    ~DockManager();

    void createFromRegistry(const ps::components::Registry &registry);
    void registerCommand(ps::commands::CommandManager &commands);
    void darkenExisting();    // ADS 自带标题栏样式，保留空实现给启动流程
    void repositionHandles(); // 自动隐藏侧栏由 ADS 负责
    void setCentralContent(QWidget *content);
    void applyTheme();

    ads::CDockWidget *byId(const QString &id) const;
    bool showDock(const QString &id, bool activate = false);
    QWidget *dockWidget(const QString &id) const;
    void toggleById(const QString &id);
    bool isVisibleById(const QString &id) const;

    QByteArray saveLayout() const;
    bool restoreLayout(const QByteArray &state);

    void toArea(ads::CDockWidget *dock, Qt::DockWidgetArea area);
    void collapse(ads::CDockWidget *dock);
    void toggleMax(ads::CDockWidget *dock);
    void restoreMax();

private:
    void ensure();
    void installCentral();

    QMainWindow *m_window = nullptr;
    ads::CDockManager *m_dock = nullptr;
    QWidget *m_centralContent = nullptr;
    bool m_centralInstalled = false;
    ads::CDockWidget *m_maxDock = nullptr;
    QList<ads::CDockWidget *> m_maxHidden;
    QWidget *m_maxCentral = nullptr;
};
