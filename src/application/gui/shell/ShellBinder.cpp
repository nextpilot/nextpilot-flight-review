#include "ShellBinder.h"

#include "Intent.h"

// ADS（vendored）：用相对路径引入，避开与本目录同名文件的歧义。
#include "../../../3rdparty/ads/src/DockAreaWidget.h"
#include "../../../3rdparty/ads/src/DockManager.h"
#include "../../../3rdparty/ads/src/DockWidget.h"
#include "../../../3rdparty/ads/src/DockWidgetTab.h"
#include "../../../3rdparty/ads/src/ads_globals.h"

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
#include <QStackedWidget>
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
    if (area == QStringLiteral("dock.diagnostics"))
        return ads::RightDockWidgetArea; // 诊断面板与属性面板同侧
    return ads::LeftDockWidgetArea;
}

// 图标名 -> QIcon。资源由外壳统一管理（:/icons/<name>.svg）。
// 动作只声明图标名，具体资源不外泄——于是模块/扩展包不依赖 GUI 资源，
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

ShellBinder::ShellBinder(QMainWindow *window, nextpilot::plotscope::registry::UiSlotHub &slotReg)
    : m_window(window), m_slots(slotReg)
{}

ShellBinder::~ShellBinder()
{
    // ADS 管理中央控件与各 dock 的父子关系，必须先于 MainWindow 子对象销毁。
    delete m_dock;
    m_dock = nullptr;
}

namespace
{
// 把某个 dock 的**内容**真正切到前台。
//
// 为什么要绕开 ADS：CDockAreaWidget::setCurrentIndex() 开头有一条早退
//   if (cw == nw && !nw->isHidden()) return;
// index 与内容栈可能已错位（index 指向目标、栈里还是别人），早退就吞掉切换，
// currentChanged 也不发——任何"在 signal 上兜底"的做法都救不回来（实测点页签不换内容）。
// 这里直接按 content 的祖先链找它所在的 QStackedWidget 再切，
// 不用 area->findChild（会命中别的嵌套栈，Qt 报 "not contained in stack"）。
void activateDockContent(ads::CDockWidget *dw)
{
    if (!dw)
        return;
    if (auto *area = dw->dockAreaWidget())
        area->setCurrentDockWidget(dw);
    QWidget *content = dw->widget();
    if (!content)
        return;
    for (QWidget *p = content->parentWidget(); p; p = p->parentWidget())
    {
        if (auto *stack = qobject_cast<QStackedWidget *>(p))
        {
            // 只认**真正装着 content**的那层栈：父链上可能先遇到别的嵌套栈。
            if (stack->indexOf(content) >= 0)
            {
                stack->setCurrentWidget(content);
                break;
            }
        }
    }
    content->show();
    content->update();
}
} // namespace

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
    // ADS 构造时已装上自己的 linux 浅色样式表；其中 active 页签的标题用
    // palette(foreground)，在本主题下解析成白字 → 选中页签"白底白字"看不清。
    // 追加同等特异性、位置更靠后的规则覆盖它（QSS 后到者胜），标题一律深色。
    m_dock->setStyleSheet(m_dock->styleSheet() +
                          QStringLiteral("ads--CDockWidgetTab[activeTab=\"true\"] QLabel "
                                         "{ color: #1f2328; }"
                                         "ads--CDockWidgetTab QLabel { color: #3a3f45; }"));
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
    // 指向它。于是重建后新 category 排不上号，内容被孤儿 category 挡住，
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
            // 两行排布：避免按钮被挤成一行细条。
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

            // 图库：单行模式 + 按声明顺序分组 + 默认显示第一组。
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
    //   · 内容是在窗口显示**之后**才填的（插槽来自模块的 onAttach），
    //     激活事件早已过去；
    //   · 无显示环境（offscreen）根本没有激活事件。
    // 结果就是：ribbon 有 1440px 宽，内容却按空壳时的 sizeHint 挤在左上角。
    //
    // **内容填完后必须让 ribbon 重新布局一次**。
    //
    // SARibbon 把按钮/面板摆在哪，是 SARibbonBarLayout::doLayout() 按
    // category 的 sizeHint 手工算的；而 sizeHint 只在内容变化后重算才更新。
    // SARibbon 自己在 QEvent::WindowActivate 时会 relayout——但那是"窗口被
    // 激活"时的事：内容是在窗口显示**之后**才填的（插槽来自模块 onAttach），
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
    // installCentral() 在 ensure() 里紧跟 CDockManager 构造就装中央区：
    // 中央区内容先到就先装中央区，侧边区永远后装。
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
    {
        // 容器透明；页签文字显式给深色——ADS 的 active 页签用浅色渐变底，
        // 若沿用深色主题的文字角色就是"白底白字"，实测选中页签标题几乎看不见。
        // ADS 的标题 QLabel objectName 固定为 dockWidgetTabLabel（DockWidgetTab.cpp:263）。
        tab->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
        // 页签点击**在 clicked 上强制同步**，不依赖 ADS 的 setCurrentIndex 早退路径：
        // 该函数开头 `if (cw == nw && !nw->isHidden()) return;` 时 index 与内容栈的
        // currentWidget 可能已经错位（index=目标、栈里仍是别人），早退就吞掉切换，
        // currentChanged 也不发——在 signal 上兜底救不回来。这里直接改内容栈。
        for (auto *tabWidget : tab->findChildren<ads::CDockWidgetTab *>())
        {
            QObject::connect(
                tabWidget, &ads::CDockWidgetTab::clicked, tabWidget,
                [tabWidget]() {
                    activateDockContent(tabWidget->dockWidget());
                },
                Qt::UniqueConnection);
        }
        // 标题直接用内联样式压住：ADS 全局 stylesheet 里 active 页签走
        // palette(foreground)，在浅色主题下会把标题刷成白字（白底白字看不清）。
        // 内联样式优先级最高，且不受 QTabBar 选中态换 palette 的影响。
        if (auto *label = dock->findChild<QLabel *>(QStringLiteral("dockWidgetTabLabel")))
            label->setStyleSheet(QStringLiteral("color: #1f2328; background: transparent;"));
    }
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
    // firstOfArea = 该侧第一个面板：只有它负责"抢占初始焦点"。
    // 后续面板只加进 tab，不抢当前显示——否则启动后显示哪个面板取决于装配顺序
    // （谁最后装配谁露脸），注册顺序一变（例如新增扩展包改了链接顺序）默认面板就跟着变。
    const bool firstOfArea = !m_lastArea.contains(area);
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
    // 新面板一律前置，并且**显式同步 tab 与内容**：
    // ADS 的 addDockWidgetTabToArea 只保证"加进去了"，不保证 tab 高亮跟着走；
    // 实测出现过"tab 高亮在『脚本文件』、内容区却是『数据源』"的错位
    // （点一下 tab 才对齐），这里由绑定器统一收口，不依赖 ADS 默认行为。
    if (dockArea)
    {
        // 页签切换后**强制 show 目标内容**：
        // ADS 的 setCurrentIndex() 里有 `if (cw == nw && !nw->isHidden()) return;`
        // 早退——当内容栈的 currentWidget 已经是目标、而该 widget 处于 hidden 时，
        // 点页签只会移动高亮，内容不重画（实测"点『数据源』页签，左栏仍是脚本库"）。
        // 这里在 currentChanged 上兜底 show，切页签一律以内容为准。
        QObject::connect(dockArea, &ads::CDockAreaWidget::currentChanged, dockArea,
                         [dockArea](int index) {
                             ads::CDockWidget *target = dockArea->dockWidget(index);
                             if (!target)
                                 return;
                             if (QWidget *content = target->widget())
                             {
                                 content->show();
                                 content->update();
                             }
                         });
        // 延迟一拍再置前：布局恢复（restoreState）期间 ADS 会**静默忽略**
        // setCurrentDockWidget（isRestoringState() 直接 return），于是"加进去的是 A、
        // 高亮留在 B"就固定下来了——启动后点页签才对齐。放到事件循环下一拍，
        // 恢复已结束，置前真正生效；QPointer 防 dock 在期间被销毁。
        if (firstOfArea)
        {
            // 首帧事件循环里 ADS 还在收尾（tab 插入会把新页签排到前面），0ms 置前会被
            // 后续装配覆盖；给一拍余量再置前，保证"第一个装配的面板"就是初始显示面板。
            QTimer::singleShot(200, dock, [dock]() {
                if (!dock)
                    return;
                activateDockContent(dock);
            });
        }
    }

    // 侧边停靠区**不隐藏标题栏**：ADS 的拖动抓手就是标题栏（单面板时没有 tab 行可拖），
    // 一旦 HideSingleWidgetTitleBar=true，面板就只剩内容区，用户既拖不动也浮不起来——
    // 那样 ADS 的停靠/浮动能力就失效了。只对中央区设这个 flag，侧边区一律保留标题栏。
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
    // 的语义不符。故使用 addDockWidget(CenterDockWidgetArea, ...)。
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

bool ShellBinder::focusPanel(QWidget *panel)
{
    if (!panel)
        return false;
    // 直查内容 widget -> dock 页签映射（ADS 自带 map 按 objectName 索引不可靠）。
    // 不走 CDockWidget::raise()：它先判 isClosed()，面板曾关过页签（closed 但未销毁）时
    // raise 会被静默跳过——表现就是"点页签/请求聚焦，内容不跟着切"。
    // 这里直接对 dockArea 设 current，语义与用户点页签完全一致。
    auto activate = [](ads::CDockWidget *dw) {
        if (!dw)
            return false;
        if (!dw->dockAreaWidget())
            return false;
        activateDockContent(dw); // 与页签点击同一口径：高亮与内容栈一起换
        return true;
    };
    if (activate(m_dockByWidget.value(panel, nullptr)))
        return true;
    // 兜底：映射表没命中（面板被重建/换过实例）时按 content 指针逐个扫。
    for (auto it = m_docksByArea.constBegin(); it != m_docksByArea.constEnd(); ++it)
        for (ads::CDockWidget *dw : it.value())
            if (dw && dw->widget() == panel && activate(dw))
                return true;
    return false;
}

bool ShellBinder::focusPanelByTitle(const QString &area, const QString &title)
{
    for (ads::CDockWidget *dw : m_docksByArea.value(area))
    {
        if (dw && dw->windowTitle() == title)
        {
            activateDockContent(dw);
            return true;
        }
    }
    return false;
}

void ShellBinder::raisePanel(QWidget *panel)
{
    focusPanel(panel);
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

QString ShellBinder::activePanelTitle(const QString &area) const
{
    const QList<ads::CDockWidget *> docks = m_docksByArea.value(area);
    if (docks.isEmpty() || !docks.first())
        return QString();
    ads::CDockAreaWidget *dockArea = docks.first()->dockAreaWidget();
    if (!dockArea)
        return QString();
    ads::CDockWidget *cur = dockArea->currentDockWidget();
    return cur ? cur->windowTitle() : QString();
}

QString ShellBinder::visiblePanelTitle(const QString &area) const
{
    for (ads::CDockWidget *dw : m_docksByArea.value(area))
    {
        if (!dw)
            continue;
        QWidget *content = dw->widget();
        if (content && content->isVisible())
            return dw->windowTitle();
    }
    return QString();
}

QStringList ShellBinder::contentsOrder(const QString &area) const
{
    QStringList out;
    const QList<ads::CDockWidget *> docks = m_docksByArea.value(area);
    if (docks.isEmpty() || !docks.first())
        return out;
    ads::CDockAreaWidget *dockArea = docks.first()->dockAreaWidget();
    if (!dockArea)
        return out;
    for (int i = 0; i < dockArea->dockWidgetsCount(); ++i)
    {
        ads::CDockWidget *dw = dockArea->dockWidget(i);
        out << (dw ? dw->windowTitle() : QString());
    }
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
