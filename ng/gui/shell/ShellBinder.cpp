#include "ShellBinder.h"

#include "Intent.h"

// ADS（vendored）：用相对路径引入，避开与本目录同名文件的歧义。
#include "../../src/3rdparty/ads/src/DockAreaWidget.h"
#include "../../src/3rdparty/ads/src/DockManager.h"
#include "../../src/3rdparty/ads/src/DockWidget.h"
#include "../../src/3rdparty/ads/src/ads_globals.h"

// SARibbon（vendored，MIT）
#include "SARibbonBar.h"
#include "SARibbonCategory.h"
#include "SARibbonPanel.h"

#include <QAction>
#include <QMainWindow>
#include <QObject>

using namespace ng::core;

namespace ng::gui
{

namespace
{
// 插槽 area -> ADS 区域
ads::DockWidgetArea adsAreaFor(const QString &area)
{
    if (area == QStringLiteral("dock.right"))
        return ads::RightDockWidgetArea;
    if (area == QStringLiteral("dock.bottom"))
        return ads::BottomDockWidgetArea;
    if (area == QStringLiteral("dock.top"))
        return ads::TopDockWidgetArea;
    return ads::LeftDockWidgetArea;
}
} // namespace

ShellBinder::ShellBinder(QMainWindow *window, ng::core::UiSlotRegistry &slotReg)
    : m_window(window), m_slots(slotReg)
{}

ShellBinder::~ShellBinder()
{
    // ADS 管理中央控件与各 dock 的父子关系，必须先于 MainWindow 子对象销毁。
    delete m_dock;
    m_dock = nullptr;
}

QWidget *ShellBinder::createRibbon()
{
    if (m_ribbon)
        return m_ribbon;

    auto *ribbon = new SARibbonBar(m_window);
    m_ribbon = ribbon;
    // 结构层交给 SARibbon：页签溢出、键盘导航、面板布局都是它的事。
    ribbon->setRibbonStyle(SARibbonBar::RibbonStyleLooseSingleRow);
    ribbon->setTitleVisible(false);
    ribbon->setTitleBarHeight(0, true);
    ribbon->setTabBarHeight(26);
    ribbon->setPanelTitleHeight(14);
    ribbon->setEnableShowPanelTitle(true);
    ribbon->setCategoryHeight(78, true);

    rebuildRibbon();
    return ribbon;
}

void ShellBinder::rebuildRibbon()
{
    if (!m_ribbon)
        return;

    // 清空旧页签（重建是幂等的）。
    const auto categories = m_ribbon->categoryPages();
    for (SARibbonCategory *category : categories)
        m_ribbon->removeCategory(category);

    // 插槽三级映射：RibbonPage → Category，RibbonGroup → Panel，UiAction → QAction
    for (const ng::core::UiSlot &pageSlot : m_slots.slotsOfKind(ng::core::SlotKind::RibbonPage))
    {
        SARibbonCategory *category = m_ribbon->addCategoryPage(pageSlot.label);
        if (!category)
            continue;

        for (const ng::core::UiSlot &groupSlot : m_slots.childrenOf(pageSlot.area))
        {
            if (groupSlot.kind != ng::core::SlotKind::RibbonGroup)
                continue;
            SARibbonPanel *panel = category->addPanel(groupSlot.label);
            if (!panel)
                continue;

            const auto actions = m_slots.actionsIn(groupSlot.area);
            for (int i = 0; i < actions.size(); ++i)
            {
                const ng::core::UiAction &ua = actions[i].action;
                auto *action = new QAction(ua.label, m_ribbon);
                action->setToolTip(ua.label);
                action->setData(ua.id);

                // 第一个动作做大按钮，其余小按钮——与常见 ribbon 观感一致。
                if (i == 0)
                    panel->addLargeAction(action);
                else
                    panel->addSmallAction(action);

                // 点击 → 意图调度。本类不解释动作语义，只把 id 交出去。
                const QString actionId = ua.id;
                QObject::connect(action, &QAction::triggered, m_ribbon, [this, actionId] {
                    m_actionTriggers[actionId]++;
                    if (m_dispatch)
                        m_dispatch(actionId);
                });
            }
        }
    }
}

void ShellBinder::dock(const QString &area, const QString &title, QWidget *content)
{
    if (!content)
        return;
    if (!m_dock)
    {
        // ADS 在构造时按静态 flag 决定行为，必须先行设置。
        ads::CDockManager::setConfigFlag(ads::CDockManager::FocusHighlighting);
        ads::CDockManager::setConfigFlag(ads::CDockManager::ActiveTabHasCloseButton, false);
        ads::CDockManager::setConfigFlag(ads::CDockManager::AllTabsHaveCloseButton, false);
        ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaDynamicTabsMenuButtonVisibility);
        ads::CDockManager::setConfigFlag(ads::CDockManager::OpaqueSplitterResize);
        ads::CDockManager::setAutoHideConfigFlags(ads::CDockManager::DefaultAutoHideConfig);
        m_dock = new ads::CDockManager(m_window);
    }

    auto *dock = new ads::CDockWidget(title, m_dock);
    dock->setObjectName(QStringLiteral("ng.dock.%1").arg(area));
    dock->setWidget(content);
    dock->setFeature(ads::CDockWidget::DockWidgetClosable, true);
    dock->setFeature(ads::CDockWidget::DockWidgetMovable, true);
    dock->setFeature(ads::CDockWidget::DockWidgetFloatable, true);

    // 同侧第二个及以后的面板并成标签页，不向下堆叠。
    ads::CDockAreaWidget *dockArea = nullptr;
    if (m_lastArea.contains(area))
    {
        dockArea = m_dock->addDockWidgetTabToArea(dock, m_lastArea.value(area));
    }
    else
    {
        dockArea = m_dock->addDockWidget(adsAreaFor(area), dock);
        m_lastArea.insert(area, dockArea);
        m_panels.insert(area, dock);
    }
    if (dockArea)
        dockArea->setDockAreaFlag(ads::CDockAreaWidget::HideSingleWidgetTitleBar, true);
}

void ShellBinder::setCentralContent(QWidget *content)
{
    m_centralContent = content;
    if (!m_dock || m_centralInstalled || !content)
        return;

    // 中央内容装进一个不可关闭/移动/浮动的 ADS dock：它就是"画布区"。
    auto *central = new ads::CDockWidget(QStringLiteral("主视图"), m_dock);
    central->setWidget(content);
    central->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    central->setFeature(ads::CDockWidget::DockWidgetMovable, false);
    central->setFeature(ads::CDockWidget::DockWidgetFloatable, false);
    auto *area = m_dock->addDockWidget(ads::CenterDockWidgetArea, central);
    if (area)
        area->setDockAreaFlag(ads::CDockAreaWidget::HideSingleWidgetTitleBar, true);
    m_centralInstalled = true;
}

void ShellBinder::setPanelVisible(const QString &area, bool visible)
{
    if (ads::CDockWidget *dock = m_panels.value(area, nullptr))
        dock->toggleView(visible);
}

bool ShellBinder::isPanelVisible(const QString &area) const
{
    ads::CDockWidget *dock = m_panels.value(area, nullptr);
    return dock && !dock->isClosed();
}

QWidget *ShellBinder::panelWidget(const QString &area) const
{
    ads::CDockWidget *dock = m_panels.value(area, nullptr);
    return dock ? dock->widget() : nullptr;
}

QString ShellBinder::panelTitle(const QString &area) const
{
    ads::CDockWidget *dock = m_panels.value(area, nullptr);
    return dock ? dock->windowTitle() : QString();
}

int ShellBinder::actionTriggerCount(const QString &actionId) const
{
    return m_actionTriggers.value(actionId, 0);
}

QJsonObject ShellBinder::captureState() const
{
    // 声明式快照：只描述"结果长什么样"，不含任何操作序列。
    // 于是恢复时缺失的部分可以安全降级（数据没打开、扩展没装上……）。
    QJsonObject state;
    if (m_window)
    {
        state.insert(QStringLiteral("windowGeometry"),
                     QString::fromLatin1(m_window->saveGeometry().toBase64()));
        state.insert(QStringLiteral("windowState"),
                     QString::fromLatin1(m_window->saveState().toBase64()));
    }
    // ADS 自带布局：dock 位置/标签合并/隐藏状态
    const QByteArray adsState = m_dock ? m_dock->saveState() : QByteArray();
    state.insert(QStringLiteral("adsLayout"), QString::fromLatin1(adsState.toBase64()));
    // ribbon 当前页签
    state.insert(QStringLiteral("ribbonPage"), m_currentPage);
    return state;
}

bool ShellBinder::applyState(const QJsonObject &state)
{
    if (state.isEmpty())
        return false;

    if (m_window)
    {
        const QString geom = state.value(QStringLiteral("windowGeometry")).toString();
        if (!geom.isEmpty())
        {
            const QByteArray raw = QByteArray::fromBase64(geom.toLatin1());
            m_window->restoreGeometry(raw);
        }
        const QString ws = state.value(QStringLiteral("windowState")).toString();
        if (!ws.isEmpty())
            m_window->restoreState(QByteArray::fromBase64(ws.toLatin1()));
    }

    // ADS 布局：缺失或版本不符时安全降级（保持当前布局）
    const QString ads = state.value(QStringLiteral("adsLayout")).toString();
    if (!ads.isEmpty() && m_dock)
        m_dock->restoreState(QByteArray::fromBase64(ads.toLatin1()));

    // ribbon 页签：越界则忽略（安全降级）
    const int page = state.value(QStringLiteral("ribbonPage")).toInt(-1);
    if (page >= 0 && m_ribbon)
    {
        m_ribbon->setCurrentIndex(page);
        m_currentPage = page;
    }
    return true;
}

QByteArray ShellBinder::saveLayout() const
{
    return m_dock ? m_dock->saveState() : QByteArray();
}

bool ShellBinder::restoreLayout(const QByteArray &state)
{
    return m_dock ? m_dock->restoreState(state) : false;
}

} // namespace ng::gui
