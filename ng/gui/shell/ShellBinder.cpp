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
#include "DockWidgetTab.h"
#include "SARibbonToolButton.h"

#include <QMenu>
#include <QToolButton>

#include <QAction>
#include <QEvent>
#include <QTimer>
#include <QFile>
#include <QIcon>
#include <QKeySequence>
#include <QMainWindow>
#include <QObject>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;

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
    if (name.isEmpty())
        return QIcon();
    // 资源可能没随版本补齐（模块可以声明图标名而不带资源），缺失就静默给空图标：
    // 让按钮退化为纯文字，而不是每次建 QAction 都刷一条 qt.svg 告警。
    const QString path = QStringLiteral(":/icons/%1.svg").arg(name);
    if (!QFile::exists(path))
        return QIcon();
    return QIcon(path);
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
    // 收缩能力：双击页签可把面板收起只留页签行。
    // 右下角 chevron 在 UseRibbonFrame 下会和窗口的关闭按钮叠在一起（见截图），
    // 因此不显示它——折叠功能仍在（双击页签）。
    ribbon->showMinimumModeButton(false);
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

// 布局组大按钮：选出 m×n 后把按钮的缩略图与文字改成本页实际分格（对齐 main
// buildLayoutPicker 里 act->setIcon/setText 的做法）。动作不存在时静默忽略。
void ShellBinder::updateActionStyle(const QString &actionId, const QIcon &icon, const QString &text)
{
    QAction *action = m_actionById.value(actionId, nullptr);
    if (!action)
        return;
    if (!icon.isNull())
        action->setIcon(icon);
    if (!text.isEmpty())
    {
        action->setText(text);
        action->setToolTip(QStringLiteral("%1：点击选择行×列，重排当前页画布").arg(text));
    }
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
    // 旧动作随旧 category 一起销毁，索引必须同时清空——否则留下的悬垂指针
    // 会在下一次 updateActionStyle 时被 setIcon 崩掉。
    m_actionById.clear();
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
            // 组内是否显式声明了大按钮：声明了就严格按声明摆（避免"第一个自动变大"）。
            bool hasDeclaredLarge = false;
            for (const nextpilot::plotscope::registry::BoundUiAction &bound : actions)
                if (bound.action.large && bound.action.galleryGroup.isEmpty())
                    hasDeclaredLarge = true;
            // 两行排布：老代码每组都 two-row，按钮才不会被挤成一行细条。
            panel->setPanelLayoutMode(SARibbonPanel::TwoRowMode);

            for (const nextpilot::plotscope::registry::BoundUiAction &bound : actions)
            {
                const nextpilot::plotscope::registry::UiAction &ua = bound.action;

                // 缩略图：优先按 visual 画（图型/格式），否则退回图标名。
                // 布局组的大按钮用"分格缩略图"（visual="layout:RxC"），其余按图型/格式画。
                QIcon ico;
                if (ua.visual.startsWith(QStringLiteral("layout:")))
                {
                    const QStringList rc = ua.visual.mid(7).split(QLatin1Char('x'));
                    ico = PlotThumbnails::layout(rc.value(0).toInt(), rc.value(1).toInt());
                }
                else if (!ua.visual.isEmpty())
                {
                    ico = PlotThumbnails::plot(ua.visual);
                }
                if (ico.isNull() && !ua.icon.isEmpty())
                    ico = iconFor(ua.icon);

                auto *action = new QAction(ico, ua.label, m_ribbon);
                action->setToolTip(ua.label);
                action->setData(ua.id);
                // 切换型动作（网格/边框/图例/工具模式）：按钮自己维护勾选态。
                if (ua.checkable)
                    action->setCheckable(true);
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

                m_actionById.insert(ua.id, action);

                if (!ua.galleryGroup.isEmpty())
                {
                    if (!galleryActions.contains(ua.galleryGroup))
                        galleryOrder << ua.galleryGroup;
                    galleryActions[ua.galleryGroup].append(action);
                }
                else
                {
                    // 尺寸由动作自己声明（large=一组的主入口，竖占两行）；没声明的退回
                    // "第一个当大按钮"的老规则，兼容早期只写了 order 的贡献者。
                    const bool useLarge = ua.large || (!ua.large && ordinaryIndex == 0
                                                       && !hasDeclaredLarge);
                    if (useLarge)
                        panel->addLargeAction(action);
                    else
                        panel->addSmallAction(action);
                    ++ordinaryIndex;

                    // 有下拉内容的动作：把菜单挂到 SARibbon 已建的按钮上，
                    // 按钮自带下拉箭头；InstantPopup 让单击直接展开选项。
                    QMenu *menu = m_actionMenuProvider ? m_actionMenuProvider(ua.id) : nullptr;
                    if (menu)
                    {
                        for (SARibbonToolButton *btn : panel->findChildren<SARibbonToolButton *>())
                        {
                            if (btn->defaultAction() != action)
                                continue;
                            btn->setMenu(menu);
                            btn->setPopupMode(QToolButton::InstantPopup);
                            break;
                        }
                    }
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
    // objectName 必须唯一：dockWidgetsMap/findDockWidget 按名索引，同区多面板
    // 撞名会让后面的覆盖前面的（"脚本调试"曾被"属性"顶掉）。加序号消歧。
    dock->setObjectName(QStringLiteral("ng.dock.%1.%2").arg(area).arg(m_dockSeq++));
    m_dockByWidget.insert(content, dock);
    m_docksByArea[area].append(dock); // 该区页签序列（panelTitles 断言/诊断用）
    // 标题栏图标：一眼区分"数据集/属性/日志"三块侧栏。
    static const struct { const char *area; const char *icon; } kDockIcons[] = {
        {"dock.left", "data_table"}, {"dock.right", "panel"}, {"dock.bottom", "history"}};
    for (const auto &di : kDockIcons)
    {
        if (area == QLatin1String(di.area))
        {
            dock->setIcon(iconFor(QString::fromLatin1(di.icon)));
            break;
        }
    }
    // 标题文字不再带一块异色底：ADS 的 tab 背景来自它自己的 palette，
    // 全局 QSS 压不过，这里给 tab 控件自身设透明（控件级样式优先级最高，
    // 也不怕 ADS 在 palette 变化时重载它自己的 stylesheet）。
    if (auto *tab = dock->tabWidget())
        tab->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    dock->setWidget(content);
    dock->setFeature(ads::CDockWidget::DockWidgetClosable, true);
    dock->setFeature(ads::CDockWidget::DockWidgetMovable, true);
    dock->setFeature(ads::CDockWidget::DockWidgetFloatable, true);

    // 侧边栏宽度约束：ADS 按内容的 sizeHint 分配初始宽度，而数据集树/属性面板
    // 的 sizeHint 会随内容（长列名、长字段说明）一直涨，结果中央画布被挤到不足一半，
    // 屏幕一大就更明显。这里给侧边内容一个上限（中央区在 installCentral 里
    // 已保证 minimumWidth 640），于是"中央最大、两侧窄"的比例由布局自己稳定下来，
    // 用户仍可在上限内拖宽。
    if (area.startsWith(QStringLiteral("dock.")))
    {
        content->setMinimumWidth(200);
        content->setMaximumWidth(400);
    }

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
    // 侧边停靠区**不隐藏标题栏**：ADS 的拖动抓手就是标题栏（单面板时没有 tab 行可拖），
    // 一旦 HideSingleWidgetTitleBar=true，面板就只剩内容区，用户既拖不动也浮不起来——
    // 那样 ADS 就白引了。老代码（src/application/shell/DockManager.cpp）也只对
    // **中央区**设这个 flag，侧边区一律保留标题栏。
    // 多面板合并成 tab 时标题栏自然出现，无需在此另设。
}

void ShellBinder::setCentralContent(QWidget *content, const QString &title)
{
    if (!content)
        return;
    ensureDockManager();

    if (!m_centralInstalled)
        installCentral();

    if (m_centralContent)
    {
        // 中央区已有真实内容（画布已就位）：后来者并成**页签**，与画布平级。
        // 典型场景：脚本编辑器请求 canvas.center——写代码需要全宽，
        // 挤在侧栏（400px 上限）里没法用。ADS 页签机制现成，零新布局代码。
        auto *dock = new ads::CDockWidget(m_dock, title.isEmpty() ? QStringLiteral("视图") : title);
        dock->setObjectName(QStringLiteral("ng.dock.central.tab.%1").arg(m_dockSeq++));
        m_dockByWidget.insert(content, dock);
        content->setMinimumWidth(320);
        dock->setWidget(content);
        dock->setMinimumWidth(320);
        // 页签可关闭/拖动/浮动；中央 dock 本体（画布）仍不可关——保持
        // "绘图分析是核心"的锚，画布页签永远在。
        dock->setFeature(ads::CDockWidget::DockWidgetClosable, true);
        dock->setFeature(ads::CDockWidget::DockWidgetMovable, true);
        dock->setFeature(ads::CDockWidget::DockWidgetFloatable, true);
        if (ads::CDockAreaWidget *area = m_centralDock->dockAreaWidget())
            m_dock->addDockWidgetTabToArea(dock, area);
        return;
    }

    // 首个中央内容：把占位容器换成本体（原有行为）。
    if (m_centralDock)
    {
        QWidget *placeholder = m_centralDock->takeWidget();
        if (placeholder)
            placeholder->deleteLater();
        m_centralDock->setWidget(content);
    }
    m_centralContent = content;
}

void ShellBinder::applyDockProportions()
{
    if (!m_dock || !m_centralDock || m_proportionsApplied)
        return;
    ads::CDockAreaWidget *area = m_centralDock->dockAreaWidget();
    if (!area)
        return;

    // 中央区所在的横向分割器：子项顺序与屏幕上从左到右一致。
    const QList<int> sizes = m_dock->splitterSizes(area);
    if (sizes.size() < 2)
        return;                                   // 只有中央，没有侧边
    int total = 0;
    for (int v : sizes)
        total += v;
    if (total < 800)
        return;                                   // 窗口还没真正布局出来

    // 最大的那块当作中央（初始它就应该最大），两侧各封顶 300px，余下全部归中央。
    int centralIdx = 0;
    for (int i = 1; i < sizes.size(); ++i)
        if (sizes.at(i) > sizes.at(centralIdx))
            centralIdx = i;

    QList<int> want = sizes;
    int sides = 0;
    for (int i = 0; i < sizes.size(); ++i)
    {
        if (i == centralIdx)
            continue;
        want[i] = qMin(sizes.at(i), 300);
        sides += want[i];
    }
    want[centralIdx] = qMax(480, total - sides);
    m_dock->setSplitterSizes(area, want);
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

void ShellBinder::raisePanel(QWidget *panel)
{
    if (!panel)
        return;
    // 直查内容 widget -> dock 页签映射（ADS 自带 map 按 objectName 索引不可靠）。
    if (ads::CDockWidget *dw = m_dockByWidget.value(panel, nullptr))
        dw->raise();
}

QList<QWidget *> ShellBinder::panelWidgets(const QString &area) const
{
    ads::CDockWidget *first = m_panels.value(area, nullptr);
    if (!first)
        return {};
    QList<QWidget *> out;
    ads::CDockAreaWidget *areaWidget = first->dockAreaWidget();
    if (!areaWidget)
    {
        out.append(first->widget());
        return out;
    }
    const QList<ads::CDockWidget *> docks = areaWidget->dockWidgets();
    for (ads::CDockWidget *dw : docks)
        out.append(dw->widget());
    return out;
}

QList<QWidget *> ShellBinder::centralWidgets() const
{
    if (!m_centralDock || !m_centralDock->dockAreaWidget())
        return {};
    QList<QWidget *> out;
    for (ads::CDockWidget *dw : m_centralDock->dockAreaWidget()->dockWidgets())
        out.append(dw->widget());
    return out;
}

QStringList ShellBinder::centralTitles() const
{
    if (!m_centralDock || !m_centralDock->dockAreaWidget())
        return {};
    QStringList out;
    for (ads::CDockWidget *dw : m_centralDock->dockAreaWidget()->dockWidgets())
        out.append(dw->windowTitle());
    return out;
}

QString ShellBinder::panelTitle(const QString &area) const
{
    ads::CDockWidget *dock = m_panels.value(area, nullptr);
    return dock ? dock->windowTitle() : QString();
}

QStringList ShellBinder::panelTitles(const QString &area) const
{
    QStringList out;
    for (ads::CDockWidget *dw : m_docksByArea.value(area))
        out << dw->windowTitle();
    return out;
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
