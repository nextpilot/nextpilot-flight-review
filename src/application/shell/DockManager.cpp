#include "DockManager.h"
#include "CommandManager.h"
#include "Component.h"
#include "../../3rdparty/ads/src/DockManager.h"
#include "../../3rdparty/ads/src/DockWidget.h"
#include "../../3rdparty/ads/src/DockAreaWidget.h"

#include <QAction>
#include <QDockWidget>
#include <QIcon>
#include <QMainWindow>
#include <QToolButton>
#include <QWidget>

namespace
{
ads::DockWidgetArea toAdsArea(Qt::DockWidgetArea area)
{
    switch (area)
    {
    case Qt::LeftDockWidgetArea: return ads::LeftDockWidgetArea;
    case Qt::RightDockWidgetArea: return ads::RightDockWidgetArea;
    case Qt::TopDockWidgetArea: return ads::TopDockWidgetArea;
    case Qt::BottomDockWidgetArea: return ads::BottomDockWidgetArea;
    default: return ads::LeftDockWidgetArea;
    }
}
} // namespace

DockManager::DockManager(QMainWindow *window) : m_window(window) {}

DockManager::~DockManager()
{
    // ADS 管理中央控件与各 dock 的父子关系，必须先于 MainWindow 子对象销毁
    delete m_dock;
    m_dock = nullptr;
}

void DockManager::ensure()
{
    if (m_dock)
        return;
    if (!m_centralContent)
        m_centralContent = m_window->centralWidget();
    // ADS 在构造时按静态 flag 决定是否创建焦点控制器，必须先行设置
    ads::CDockManager::setConfigFlag(ads::CDockManager::FocusHighlighting);
    // 标签不画内联关闭按钮，关闭入口只留 DockArea 右侧的叉（对齐 VS 观感）
    ads::CDockManager::setConfigFlag(ads::CDockManager::ActiveTabHasCloseButton, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::AllTabsHaveCloseButton, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaDynamicTabsMenuButtonVisibility);
    ads::CDockManager::setConfigFlag(ads::CDockManager::OpaqueSplitterResize);
    ads::CDockManager::setAutoHideConfigFlags(ads::CDockManager::DefaultAutoHideConfig);
    m_dock = new ads::CDockManager(m_window);
    installCentral();
}

void DockManager::setCentralContent(QWidget *content)
{
    m_centralContent = content;
    installCentral();
}

void DockManager::installCentral()
{
    if (!m_dock || m_centralInstalled)
        return;
    QWidget *content = m_centralContent;
    if (!content || content == m_dock)
        return;
    auto *central = new ads::CDockWidget(QStringLiteral("主视图"), m_dock);
    content->setMinimumWidth(640);
    central->setWidget(content);
    central->setMinimumWidth(640);
    central->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    central->setFeature(ads::CDockWidget::DockWidgetMovable, false);
    central->setFeature(ads::CDockWidget::DockWidgetFloatable, false);
    auto *area = m_dock->addDockWidget(ads::CenterDockWidgetArea, central);
    if (area)
        area->setDockAreaFlag(ads::CDockAreaWidget::HideSingleWidgetTitleBar, true);
    m_centralInstalled = true;
}

void DockManager::createFromRegistry(const ps::components::Registry &registry)
{
    ensure();
    auto docks = registry.docks();
    std::sort(docks.begin(), docks.end(),
              [](const ps::components::DockContribution &a, const ps::components::DockContribution &b) {
                  return a.order < b.order;
              });
    QHash<int, ads::CDockAreaWidget *> lastArea;
    for (const auto &spec : docks)
    {
        QWidget *created = spec.create ? spec.create(m_window) : nullptr;
        if (!created)
            continue;
        // 旧模块可能还返回 QDockWidget，这里只取它的内容控件
        QWidget *content = created;
        if (auto *old = qobject_cast<QDockWidget *>(created))
            content = old->widget();
        if (!content)
            continue;

        auto *dock = new ads::CDockWidget(spec.title, m_dock);
        if (!spec.id.isEmpty())
            dock->setObjectName(spec.id);
        const QString icon = !spec.icon.isEmpty()
                                 ? spec.icon
                                 : created->property("psIcon").toString();
        if (!icon.isEmpty())
            dock->setIcon(QIcon(icon));
        dock->setWidget(content);
        dock->setFeature(ads::CDockWidget::DockWidgetClosable, true);
        dock->setFeature(ads::CDockWidget::DockWidgetMovable, true);
        dock->setFeature(ads::CDockWidget::DockWidgetFloatable, true);
        ads::CDockAreaWidget *area = nullptr;
        const int key = static_cast<int>(spec.area);
        if (lastArea.contains(key))
        {
            // 同侧第二块：右侧向下堆叠，其余并成标签
            if (spec.area == Qt::RightDockWidgetArea)
                area = m_dock->addDockWidget(ads::BottomDockWidgetArea, dock, lastArea.value(key));
            else
                area = m_dock->addDockWidget(ads::CenterDockWidgetArea, dock, lastArea.value(key));
        }
        else
        {
            area = m_dock->addDockWidget(toAdsArea(spec.area), dock);
        }
        if (area)
            lastArea.insert(key, area);
        dock->toggleView(spec.visible);
        if (spec.layout)
            spec.layout(m_window);
    }
}

void DockManager::darkenExisting() {}

void DockManager::repositionHandles() {}

ads::CDockWidget *DockManager::byId(const QString &id) const
{
    if (!m_dock)
        return nullptr;
    auto it = m_dock->dockWidgetsMap().find(id);
    return it == m_dock->dockWidgetsMap().end() ? nullptr : it.value();
}

bool DockManager::isVisibleById(const QString &id) const
{
    auto *dock = byId(id);
    return dock && !dock->isClosed();
}

void DockManager::toggleById(const QString &id)
{
    if (auto *dock = byId(id))
        dock->toggleView(!dock->isClosed());
}

void DockManager::registerCommand(ps::commands::CommandManager &commands)
{
    commands.registerCommand(
        ps::commands::CommandSpec{QStringLiteral("view.toggleDock"), QStringLiteral("显示/隐藏面板"),
                                  {}, {}, true, {}, [this](const ps::commands::CommandContext &ctx) {
                                      return isVisibleById(ctx.arg);
                                  }, true},
        [this](const ps::commands::CommandContext &ctx) { toggleById(ctx.arg); });
}

QByteArray DockManager::saveLayout() const
{
    return m_dock ? m_dock->saveState() : QByteArray();
}

bool DockManager::restoreLayout(const QByteArray &state)
{
    ensure();
    return m_dock ? m_dock->restoreState(state) : false;
}

void DockManager::toArea(ads::CDockWidget *dock, Qt::DockWidgetArea area)
{
    if (!m_dock || !dock)
        return;
    restoreMax();
    m_dock->addDockWidget(toAdsArea(area), dock);
    dock->toggleView(true);
}

void DockManager::collapse(ads::CDockWidget *dock)
{
    if (dock)
        dock->setAutoHide(true);
}

void DockManager::toggleMax(ads::CDockWidget *dock)
{
    if (!dock)
        return;
    if (m_maxDock == dock)
    {
        restoreMax();
        return;
    }
    restoreMax();
    m_maxCentral = m_centralContent;
    if (m_maxCentral)
        m_maxCentral->hide();
    m_maxHidden.clear();
    for (auto *other : m_dock->dockWidgetsMap())
        if (other != dock && !other->isClosed())
        {
            m_maxHidden.append(other);
            other->toggleView(false);
        }
    dock->toggleView(true);
    m_maxDock = dock;
}

void DockManager::restoreMax()
{
    if (!m_maxDock)
        return;
    for (auto *dock : m_maxHidden)
        if (dock)
            dock->toggleView(true);
    m_maxHidden.clear();
    if (m_maxCentral)
        m_maxCentral->show();
    m_maxDock = nullptr;
}
