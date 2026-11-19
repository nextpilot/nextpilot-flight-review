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
#include "SARibbonGallery.h"
#include "SARibbonGalleryGroup.h"
#include "SARibbonPanel.h"

#include "PlotThumbnails.h"

#include <QAction>
#include <QIcon>
#include <QKeySequence>
#include <QMainWindow>
#include <QObject>

using namespace nextpilot::plotscope::core;

namespace nextpilot::plotscope::gui
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

// 图标名 -> QIcon。资源由外壳统一管理（:/icons/<name>.svg）。
// 动作只声明图标名，具体资源不外泄——于是模块/知识包不依赖 GUI 资源，
// headless（CLI）构建保持纯净。
QIcon iconFor(const QString &name)
{
    return QIcon(QStringLiteral(":/icons/%1.svg").arg(name));
}
} // namespace

ShellBinder::ShellBinder(QMainWindow *window, nextpilot::plotscope::registry::UiSlotRegistry &slotReg)
    : m_window(window), m_slots(slotReg)
{}

ShellBinder::~ShellBinder()
{
    // ADS 管理中央控件与各 dock 的父子关系，必须先于 MainWindow 子对象销毁。
    delete m_dock;
    m_dock = nullptr;
}

void ShellBinder::ensureDockManager()
{
    if (m_dock)
        return;

    // ADS 在构造时按静态 flag 决定行为，必须先行设置。
    ads::CDockManager::setConfigFlag(ads::CDockManager::FocusHighlighting);
    ads::CDockManager::setConfigFlag(ads::CDockManager::ActiveTabHasCloseButton, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::AllTabsHaveCloseButton, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaDynamicTabsMenuButtonVisibility);
    ads::CDockManager::setConfigFlag(ads::CDockManager::OpaqueSplitterResize);
    ads::CDockManager::setAutoHideConfigFlags(ads::CDockManager::DefaultAutoHideConfig);
    m_dock = new ads::CDockManager(m_window);
}

void ShellBinder::createRibbon(SARibbonBar *ribbon)
{
    if (!ribbon)
        return;
    if (m_ribbon == ribbon)
        return;
    m_ribbon = ribbon;
    // 结构层交给 SARibbon：页签溢出、键盘导航、面板布局都是它的事。
    ribbon->setRibbonStyle(SARibbonBar::RibbonStyleLooseSingleRow);
    ribbon->setTitleVisible(false);       // 窗口标题由系统标题栏承担
    ribbon->setTitleBarHeight(0, true);    // 标题带压成 0，否则页签被推低
    ribbon->setTabBarHeight(26);
    // 收缩能力：右下角 chevron + 双击页签可把面板收起只留页签行。
    ribbon->showMinimumModeButton(true);
    ribbon->setTabDoubleClickToMinimumMode(true);
    ribbon->setPanelTitleHeight(14);
    // 分组标题画在面板底边内，category 高度必须把它算进去。
    ribbon->setEnableShowPanelTitle(true);
    ribbon->setPanelToolButtonIconSize(QSize(18, 18), QSize(26, 26));
    ribbon->setPanelSpacing(3);
    ribbon->setCategoryHeight(78, true);

    // 左上角默认带一个空 application button，留白很难看——藏掉。
    if (auto *ab = ribbon->applicationButton())
        ab->hide();

    rebuildRibbon();
}

void ShellBinder::applyTheme(bool dark, const QString &ribbonQss)
{
    if (!m_ribbon || !m_window)
        return;
    Q_UNUSED(dark);
    // SARibbon 自绘，全局 QSS 压不住它的内建配色。把 token 化的覆盖 QSS
    // 直接设给 ribbonBar 本身——颜色由 ThemeManager 生成，这里只负责套用
    // （守 §7：绑定器是唯一知道 SARibbon 的地方）。
    m_ribbon->setStyleSheet(ribbonQss);
}

void ShellBinder::rebuildRibbon()
{
    if (!m_ribbon)
        return;

    // 清空旧页签（重建是幂等的）。
    //
    // 注意：SARibbon 的 removeCategory() 只把 category 从 stacked widget 里
    // **移出**，并不销毁它——对象还在，且 stacked widget 的"当前页"仍可能
    // 指向它。于是重建后新 category 排不上号，内容被旧的孤儿挡住，
    // 表现为"内容挤在左上角一小块"。所以这里显式销毁。
    const auto categories = m_ribbon->categoryPages();
    for (SARibbonCategory *category : categories)
    {
        m_ribbon->removeCategory(category);
        category->deleteLater();
    }

    // 插槽三级映射：RibbonPage → Category，RibbonGroup → Panel，UiAction → QAction
    QVector<SARibbonCategory *> built;
    for (const nextpilot::plotscope::registry::UiSlot &pageSlot : m_slots.slotsOfKind(nextpilot::plotscope::registry::SlotKind::RibbonPage))
    {
        SARibbonCategory *category = m_ribbon->addCategoryPage(pageSlot.label);
        if (!category)
            continue;
        built << category;

        for (const nextpilot::plotscope::registry::UiSlot &groupSlot : m_slots.childrenOf(pageSlot.area))
        {
            if (groupSlot.kind != nextpilot::plotscope::registry::SlotKind::RibbonGroup)
                continue;
            const auto actions = m_slots.actionsIn(groupSlot.area);
            // 空分组不渲染：兼容保留的旧 area（如 home 下的 plottype/sources）
            // 没有 action 时若仍建 panel，会在页上留下几个只有竖排标题的空壳，很难看。
            if (actions.isEmpty())
                continue;
            SARibbonPanel *panel = category->addPanel(groupSlot.label);
            if (!panel)
                continue;


            // 一个 action 若声明了 galleryGroup，就进图库（按组名聚合），
            // 否则是普通按钮。图库项与按钮可以共存在一个分组里。
            // 这是"plot 类型 / parse 类型都能用图库选"的唯一装配点——
            // 外壳不解释图型/格式语义，只按纯数据分组呈现。
            QHash<QString, QList<QAction *>> galleryActions; // 组名 -> 项
            QStringList galleryOrder;                        // 组出现顺序
            int ordinaryIndex = 0;

            for (const nextpilot::plotscope::registry::BoundUiAction &bound : actions)
            {
                const nextpilot::plotscope::registry::UiAction &ua = bound.action;

                // 缩略图：优先按 visual 画（图型/格式），否则退回图标名。
                QIcon ico = ua.visual.isEmpty() ? QIcon() : PlotThumbnails::plot(ua.visual);
                if (ico.isNull() && !ua.icon.isEmpty())
                    ico = iconFor(ua.icon);

                auto *action = new QAction(ico, ua.label, m_ribbon);
                action->setToolTip(ua.label);
                action->setData(ua.id);
                // 快捷键是动作属性：外壳只做 setShortcut，触发仍走同一个 action id
                // → 与按钮/图库项/脚本同一条意图路径。QKeySequence 解析无效串会留空，安全。
                if (!ua.shortcut.isEmpty())
                    action->setShortcut(QKeySequence(ua.shortcut));
                const QString actionId = ua.id;
                const QString intentOp = ua.intentOp;
                const QString actionData = ua.data;
                // 点击 → 意图调度（图库项与按钮走同一条路径）。
                QObject::connect(action, &QAction::triggered, m_ribbon,
                                 [this, actionId, intentOp, actionData] {
                                     m_actionTriggers[actionId]++;
                                     if (m_dispatch)
                                         m_dispatch(actionId, intentOp, actionData);
                                 });

                if (!ua.galleryGroup.isEmpty())
                {
                    if (!galleryActions.contains(ua.galleryGroup))
                        galleryOrder << ua.galleryGroup;
                    galleryActions[ua.galleryGroup].append(action);
                }
                else
                {
                    // 第一个普通动作做大按钮，其余小按钮。
                    if (ordinaryIndex == 0)
                        panel->addLargeAction(action);
                    else
                        panel->addSmallAction(action);
                    ++ordinaryIndex;
                }
            }

            // 图库：单行模式 + 按声明顺序分组 + 默认显示第一组（抄老代码观感）。
            if (!galleryOrder.isEmpty())
            {
                panel->setPanelLayoutMode(SARibbonPanel::TwoRowMode);
                auto *gallery = panel->addGallery();
                gallery->setSingleRowMode(true);
                SARibbonGalleryGroup *firstGroup = nullptr;
                for (const QString &g : galleryOrder)
                {
                    auto *grp = gallery->addCategoryActions(g, galleryActions.value(g));
                    grp->setGalleryGroupStyle(SARibbonGalleryGroup::IconOnly);
                    grp->setDisplayRow(SARibbonGalleryGroup::DisplayOneRow);
                    grp->setGridMinimumWidth(88);
                    grp->setGridMaximumWidth(96);
                    if (!firstGroup)
                        firstGroup = grp;
                }
                if (firstGroup)
                    gallery->setCurrentViewGroup(firstGroup);
                panel->setMaximumWidth(460);
            }

            // 面板本体撑到能放下大按钮：SARibbon 的 category 高度不会
            // 自动把 panel 拉满，少了这一步按钮会挤成一小块。
            panel->setMinimumHeight(56);
        }
    }

    // **内容填完后必须显式重排几何**。
    //
    // SARibbon 的按钮/面板位置是在 SARibbonBarLayout::doLayout() 里
    // 按 category 的 sizeHint 手工摆的，而 sizeHint 只有在内容变化后
    // 重新计算才会更新。SARibbon 自己在 QEvent::WindowActivate 时会
    // relayout——但那是"窗口被激活"时的事：
    //   · 内容是在窗口显示**之后**才填的（插槽来自器官的 onAttach），
    //     激活事件早已过去；
    //   · 无显示环境（offscreen）根本没有激活事件。
    // 结果就是：ribbon 有 1440px 宽，内容却按空壳时的 sizeHint 挤在左上角。
    //
    // **内容填完后必须让 ribbon 重新布局一次**。
    //
    // SARibbon 把按钮/面板摆在哪，是 SARibbonBarLayout::doLayout() 按
    // category 的 sizeHint 手工算的；而 sizeHint 只在内容变化后重算才更新。
    // SARibbon 自己在 QEvent::WindowActivate 时会 relayout——但那是"窗口被
    // 激活"时的事：内容是在窗口显示**之后**才填的（插槽来自器官 onAttach），
    // 激活事件早已过去；无显示环境（offscreen）更是根本没有激活事件。
    // 结果：ribbon 有 1440px 宽，内容却按空壳时的 sizeHint 挤在左上角。
    //
    // SARibbonCategory::updateItemGeometry() 是官方为此提供的入口：
    // 它重算 category 内各面板的按钮几何。
    for (SARibbonCategory *category : built)
        category->updateItemGeometry();

    // 再用公开的尺寸接口触发一次 ribbon 自身的重排
    // （setCategoryHeight 的 resizeByNow 参数就是"立即重排"的意思）。
    m_ribbon->setCategoryHeight(m_ribbon->categoryHeight(), /*resizeByNow=*/true);
    m_ribbon->setPanelTitleHeight(m_ribbon->panelTitleHeight());
    m_ribbon->updateGeometry();

    // 当前页必须指向新建的 category：移出旧页后 stacked widget 的当前页
    // 可能还停在已被移除的孤儿上，新页就显示不出来。
    if (!built.isEmpty())
        m_ribbon->setCurrentIndex(0);
}

void ShellBinder::dock(const QString &area, const QString &title, QWidget *content)
{
    if (!content)
        return;
    ensureDockManager();

    // **中央区必须先于侧边区注册**——这是 ADS 的硬性布局约束。
    //
    // 原因在 ADS 的 addDockArea()：它只在"区域内 dock 数量 <= 1"时才按
    // 插入参数重设根分割器方向。所以谁先注册谁决定最终方向：
    //   先侧边区(Horizontal) → 再中央区(Vertical) → 根分割器被翻成垂直，
    //                          于是数据集树在上、画布在下；
    //   先中央区(Vertical)   → 再侧边区(Horizontal) → 根分割器保持水平，
    //                          得到「左侧树 | 中央画布」。
    //
    // 老代码（src/application/shell/DockManager.cpp）用同一个招：
    // installCentral() 在 ensure() 里紧跟 CDockManager 构造就装中央区。
    // 这里沿用该做法：中央区内容先到就先装中央区，侧边区永远后装。
    if (!m_centralInstalled)
        installCentral();

    auto *dock = new ads::CDockWidget(m_dock, title);
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
    if (!content)
        return;
    ensureDockManager();

    // 中央内容后到：侧边区可能已经装好了。但中央 dock 仍是**第一个**
    // 装进 ADS 的（dock() 里会先 installCentral() 占位），
    // 所以布局方向不会因为它晚到而跑偏。
    if (!m_centralInstalled)
        installCentral();
    else if (m_centralDock)
    {
        // 已有中央 dock：把真正的面板换进去（占位容器丢弃）。
        QWidget *placeholder = m_centralDock->takeWidget();
        if (placeholder)
            placeholder->deleteLater();
        m_centralDock->setWidget(content);
    }
    m_centralContent = content;
}

void ShellBinder::installCentral()
{
    if (!m_dock || m_centralInstalled)
        return;

    // 中央区内容此刻可能还没到（画布模块可能尚未 attach）：
    // 先用一个占位容器占住"中央"这个位置，
    // 保证后续所有侧边 dock 都排在它之后——顺序约束由外壳兜住。
    QWidget *content = m_centralContent ? m_centralContent : new QWidget();
    m_centralDock = new ads::CDockWidget(m_dock, QStringLiteral("主视图"));
    m_centralDock->setObjectName(QStringLiteral("ng.dock.canvas.center"));
    content->setMinimumWidth(320);
    m_centralDock->setWidget(content);
    m_centralDock->setMinimumWidth(320);
    m_centralDock->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    m_centralDock->setFeature(ads::CDockWidget::DockWidgetMovable, false);
    m_centralDock->setFeature(ads::CDockWidget::DockWidgetFloatable, false);

    // 用 addDockWidget(CenterDockWidgetArea) 而不是 setCentralWidget()：
    // 后者是 ADS 的"独占中央控件"入口，要求它是唯一且第一个 dock，
    // 且会把中央控件排除在普通 dock 体系之外——与"画布区是一个可停靠区域"
    // 的语义不符。老代码同样用 addDockWidget(CenterDockWidgetArea, ...)。
    auto *area = m_dock->addDockWidget(ads::CenterDockWidgetArea, m_centralDock);
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

} // namespace nextpilot::plotscope::gui
