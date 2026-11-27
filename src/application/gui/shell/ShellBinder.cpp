#include "ShellBinder.h"

#include "ActivityBar.h"
#include "ShellBinder.h"

#include "PanelReferenceValidator.h"
#include "ContributionParser.h"
#include "IEditorService.h"
#include "theme/IconRegistry.h" // A2c：缩略图查表（绘制器已归位 modules/plot/gui）

#include "Intent.h"

// ADS（vendored）：用相对路径引入，避开与本目录同名文件的歧义。
#include "../../../3rdparty/ads/src/DockAreaWidget.h"
#include "../../../3rdparty/ads/src/DockManager.h"
#include "../../../3rdparty/ads/src/DockSplitter.h"
#include "../../../3rdparty/ads/src/DockWidget.h"
#include "../../../3rdparty/ads/src/DockWidgetTab.h"
#include "../../../3rdparty/ads/src/ads_globals.h"

// SARibbon（vendored，MIT）
#include "SARibbonBar.h"
#include "SARibbonCategory.h"
#include "SARibbonGallery.h"
#include "SARibbonGalleryGroup.h"
#include "SARibbonPanel.h"

#include "DockWidgetTab.h"
#include "SARibbonToolButton.h"

#include <QMenu>
#include <QHBoxLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QToolButton>
#include <QSplitter>

#include <QAction>
#include <QEvent>
#include <QTimer>
#include <QFile>
#include <QIcon>
#include <QKeySequence>
#include <QMainWindow>
#include <QObject>
#include <QStatusBar>

#include <algorithm>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;

namespace nextpilot::plotscope::gui
{

namespace
{
// 插槽 area -> ADS 区域
// 停靠布局版本：3 = 左栏改为单一 ps.dock.sidebar。
// 旧版本快照里的 dock 树不认识新栏，恢复会把左栏/面板藏掉或留成浮动。
constexpr int kDockLayoutVersion = 4;

ads::DockWidgetArea adsAreaFor(nextpilot::plotscope::registry::Region region)
{
    using nextpilot::plotscope::registry::Region;
    switch (region)
    {
    case Region::SecondarySidebar:
        return ads::RightDockWidgetArea;
    case Region::Panel:
        return ads::BottomDockWidgetArea;
    case Region::PrimarySidebar:
    case Region::ActivityBar:
    case Region::Editor:
    default:
        return ads::LeftDockWidgetArea;
    }
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

// 找到某个 area 在指定分割器里的**直接子控件**。
//
// ADS 按分割需要会把 area 再包一层 CDockSplitter，所以 QSplitter::indexOf(area)
// 经常返回 -1；从 area 往上爬，直到父控件正好是目标分割器。
QWidget *childOfSplitter(QWidget *widget, QSplitter *splitter)
{
    if (!widget || !splitter)
        return nullptr;
    QWidget *cur = widget;
    while (cur && cur->parentWidget() != splitter)
        cur = cur->parentWidget();
    return (cur && cur->parentWidget() == splitter) ? cur : nullptr;
}
} // namespace

ShellBinder::ShellBinder(QMainWindow *window, nextpilot::plotscope::workbench::ISlotsService *slotSvc)
    : m_window(window), m_slots(slotSvc)
{
}

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
    // 保留 ADS 构造时装上的内建样式表：换肤时以它为底再叠停靠区样式。
    // （内建表里有 ADS 必需的图标 url 规则，整体替换会把关闭/浮动按钮图标弄丢。）
    m_adsBaseQss = m_dock->styleSheet();
    // 换肤信号早于本函数时（m_dock 还不存在），applyTheme 只把样式缓存下来；
    // 这里建完立刻补应用，保证第一帧就是统一外观。
    if (!m_dockQss.isEmpty())
        m_dock->setStyleSheet(m_adsBaseQss + m_dockQss);

    // ADS 构造时会把自身设为 QMainWindow 的 centralWidget。Activity Bar 要
    // 占据左侧全高，必须把 ADS 放进一个普通横向布局：ActivityBar | DockManager。
    // 状态栏仍由 QMainWindow 管，所以在状态栏之上、ribbon 之下的通栏布局。
    ensureActivityShell();
}

void ShellBinder::ensureActivityShell()
{
    if (m_shellFrame || !m_window || !m_dock)
        return;

    if (m_window->centralWidget() == m_dock)
        m_window->takeCentralWidget();

    m_shellFrame = new QWidget(m_window);
    m_shellFrame->setObjectName(QStringLiteral("ps.shellFrame"));
    auto *layout = new QHBoxLayout(m_shellFrame);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_activityBar = new ActivityBar(m_shellFrame);
    m_activityBar->setVisible(m_activityBarVisible);
    layout->addWidget(m_activityBar, 0);
    layout->addWidget(m_dock, 1);
    m_window->setCentralWidget(m_shellFrame);

    QObject::connect(m_activityBar, &ActivityBar::activityActivated, m_activityBar,
                     [this](const QString &id) { activateActivity(id); });
    QObject::connect(m_activityBar, &ActivityBar::settingsRequested, m_activityBar, [this]() {
        if (m_settingsHandler)
            m_settingsHandler();
    });
    QObject::connect(
        m_activityBar, &ActivityBar::dockDropped, m_activityBar,
        [this](ads::CDockWidget *dock, const QString &activityId) { pinDockToActivity(dock, activityId); });

    rebuildActivityBar();
}

void ShellBinder::rebuildActivityBar()
{
    if (!m_activityBar)
        return;

    // 启动期校验（验收 4）：声明侧面板类型必须在工厂表命中，否则布局会引用不存在的面板。
    // 这里只收集不打印——统一诊断由 MainWindow 装配入口输出，同一问题不打两遍；
    // 不 abort：abort 会让整个产品起不来。缺失列表由 test_gui 断言为空。
    const QStringList missing = missingPanelTypes();
    Q_UNUSED(missing);

    QList<ActivitySpec> specs = m_slots ? m_slots->activities() : QList<ActivitySpec>{};
    if (!m_activityOrder.isEmpty())
    {
        QList<ActivitySpec> ordered;
        QStringList orderedIds;
        for (const QString &id : m_activityOrder)
            for (const ActivitySpec &spec : specs)
                if (spec.id == id && !orderedIds.contains(id))
                {
                    ordered.append(spec);
                    orderedIds << id;
                }
        for (const ActivitySpec &spec : specs)
            if (!orderedIds.contains(spec.id))
            {
                ordered.append(spec);
                orderedIds << spec.id;
            }
        specs = ordered;
    }

    m_activityBar->setActivities(specs);
    if (specs.isEmpty())
        return;

    if (m_activeActivity.isEmpty()
        || std::none_of(specs.cbegin(), specs.cend(),
                        [this](const ActivitySpec &s) { return s.id == m_activeActivity; }))
        m_activeActivity = specs.first().id;
    m_activityBar->setActiveActivity(m_activeActivity);
    // 重建只同步视图显隐，不触发 Activity 的中央面板请求；
    // 否则启动装配阶段会把脚本编辑器提前创建出来。
    syncActivityViews(m_activeActivity);
}

void ShellBinder::setPanelReferenceContext(
    const QList<nextpilot::plotscope::registry::ProductPanelPlacement> &placements,
    nextpilot::plotscope::workbench::IViewsService *viewsService,
    nextpilot::plotscope::workbench::IEditorService *editorService)
{
    m_placements = placements;
    m_viewsRefService = viewsService;
    m_editorRefService = editorService;
}

QStringList ShellBinder::missingPanelTypes() const
{
    using namespace nextpilot::plotscope::registry;
    QStringList missing;

    // 已注入产品布局与声明命中面服务时走 lib 级两源校验器；否则退化为只查 Activity panelTypes。
    if (m_viewsRefService)
    {
        // views 命中面（A2b 笔2b 第②步）：已迁 ViewRegistry 的 id（gui 内置面板，
        // 注册不再走工厂表）经 IViewsService::viewForId 判声明命中——壳只查
        // 接口、不直连 ViewRegistry。旧工厂路径保留到⑦步删。
        // Activity 清单取一次共用（三处：命中面循环 + 校验器 + 计数口径不动）。
        const QList<ActivitySpec> slotActivities = m_slots ? m_slots->activities() : QList<ActivitySpec>{};
        QStringList knownViewIds;
        if (m_viewsRefService)
        {
            for (const ProductPanelPlacement &placement : m_placements)
            {
                if (placement.viewId.isEmpty())
                    continue;
                if (!m_viewsRefService->viewForId(placement.viewId).owner.isEmpty())
                    knownViewIds << placement.viewId;
            }
            for (const ActivitySpec &activity : slotActivities)
            {
                for (const QString &type : activity.panelTypes)
                    if (!type.isEmpty() && !knownViewIds.contains(type) &&
                        !m_viewsRefService->viewForId(type).owner.isEmpty())
                        knownViewIds << type;
                const QString &central = activity.centralEditor;
                if (!central.isEmpty() && !knownViewIds.contains(central) &&
                    !m_viewsRefService->viewForId(central).owner.isEmpty())
                    knownViewIds << central;
            }
        }
        // editor 命中面（A2b 笔2b 第③步顺延必修）：customEditors 声明的 viewType
        // 经 IEditorService::customEditors() 取回——centralEditor（plot.workspace /
        // script-editor 等 editor 化点）命中自此不再靠工厂表侥幸。壳只查接口。
        QStringList customEditorViewTypes;
        if (m_editorRefService)
        {
            for (const nextpilot::plotscope::registry::ContributedCustomEditor &ce :
                 m_editorRefService->customEditors())
                if (!ce.viewType.isEmpty())
                    customEditorViewTypes << ce.viewType;
        }
        // views 全量声明面（A2c）：领域包面板描述（provider=pack.*）的 id 也须命中
        // views 声明——不再依赖"恰好被 placements/activity 引用过"才可见。
        // IViewsService 无全量列举，按可停靠 area 三查合并（与落区映射同集）。
        if (m_viewsRefService)
        {
            for (const char *area : {"sidebar", "auxiliary", "bottom"})
                for (const nextpilot::plotscope::registry::ContributedViewContainer &c :
                     m_viewsRefService->containersIn(QLatin1String(area)))
                    for (const nextpilot::plotscope::registry::ContributedView &v :
                         m_viewsRefService->viewsInContainer(c.id))
                        if (!v.id.isEmpty())
                            knownViewIds << v.id;
        }
        const auto issues = validatePanelReferences(slotActivities, m_placements, knownViewIds, customEditorViewTypes);
        for (const PanelReferenceIssue &issue : issues)
            missing << QStringLiteral("%1:%2").arg(issue.source, issue.type);
        return missing;
    }
    // A2d：工厂表命中面与 IPanelsService 声明面均已随旧契约退场（第①/②/⑤步），
    // 命中面只剩 views ∪ customEditors。两个声明面服务未注入（极简装配）时
    // 视为零校验面。
    return missing;
}

QString ShellBinder::activityForPanel(const QString &pageKey) const
{
    if (m_viewActivity.contains(pageKey))
        return m_viewActivity.value(pageKey);
    for (const ActivitySpec &spec : m_slots ? m_slots->activities() : QList<ActivitySpec>{})
        if (spec.panelTypes.contains(pageKey))
            return spec.id;
    return m_activeActivity;
}

bool ShellBinder::isLeftPage(const QString &pageKey) const
{
    return m_leftPageKeys.contains(pageKey);
}

QList<ads::CDockWidget *> ShellBinder::leftDocks() const
{
    QList<ads::CDockWidget *> out;
    for (const QString &id : m_leftPageKeys)
        if (ads::CDockWidget *dock = m_dockByKey.value(id, nullptr))
            out.append(dock);
    return out;
}

void ShellBinder::syncActivityViews(const QString &activityId)
{
    bool leftHasActivity = false;
    for (const QString &id : m_leftPageKeys)
        if (activityForPanel(id) == activityId)
            leftHasActivity = true;
    // 视图可能已被拖到右槽/底槽或浮动：Activity 点击要在它当前所在位置聚焦，
    // 不能只认左槽。
    for (auto it = m_dockByKey.constBegin(); it != m_dockByKey.constEnd(); ++it)
    {
        if (activityForPanel(it.key()) != activityId)
            continue;
        it.value()->toggleView(true);
        activateDockContent(it.value());
    }
    for (const QString &id : m_leftPageKeys)
    {
        ads::CDockWidget *dock = m_dockByKey.value(id, nullptr);
        if (!dock)
            continue;
        if (!m_sideBarVisible || m_collapsedViews.contains(id))
            dock->toggleView(false);
        else if (activityForPanel(id) == activityId)
            dock->toggleView(true);
        else if (leftHasActivity)
            dock->toggleView(false);
    }
}

void ShellBinder::activateActivity(const QString &activityId)
{
    const ActivitySpec *spec = m_slots ? m_slots->activity(activityId) : nullptr;
    if (!spec)
        return;
    m_activeActivity = activityId;
    if (m_activityBar)
        m_activityBar->setActiveActivity(activityId);
    syncActivityViews(activityId);
    if (!spec->ribbonPage.isEmpty())
        setRibbonPage(spec->ribbonPage);
    if (m_activityHandler)
        m_activityHandler(*spec);
}

void ShellBinder::applyViewDockFeatures(ads::CDockWidget *dock)
{
    if (!dock)
        return;
    dock->setFeature(ads::CDockWidget::DockWidgetClosable, true);
    dock->setFeature(ads::CDockWidget::DockWidgetMovable, true);
    dock->setFeature(ads::CDockWidget::DockWidgetFloatable, true);
    // 允许区域挂在 CDockAreaWidget 上，见 dock() 里对新建 area 的设置。
}

QString ShellBinder::pageKeyForDock(ads::CDockWidget *dock) const
{
    if (!dock)
        return QString();
    for (auto it = m_dockByKey.constBegin(); it != m_dockByKey.constEnd(); ++it)
        if (it.value() == dock)
            return it.key();
    return QString();
}

void ShellBinder::pinDockToActivity(ads::CDockWidget *dock, const QString &activityId)
{
    if (!dock || !m_dock || !m_activityBar)
        return;
    QString pageKey = pageKeyForDock(dock);
    if (pageKey.isEmpty())
    {
        pageKey = dock->objectName();
        if (pageKey.isEmpty())
            return;
        m_dockByKey.insert(pageKey, dock);
    }

    QString target = activityId;
    bool created = false;
    if (target.isEmpty())
    {
        ActivitySpec spec;
        spec.id = QStringLiteral("view.") + pageKey;
        spec.label = dock->windowTitle();
        spec.icon = QStringLiteral("panel");
        spec.order = m_slots ? m_slots->activities().size() : 0;
        spec.panelTypes << pageKey;
        spec.provider = QStringLiteral("user");
        if (m_slots)
            m_slots->registerActivity(spec);
        target = spec.id;
        created = true;
    }
    m_viewActivity.insert(pageKey, target);

    // 拖到 Activity Bar = 该视图归入对应容器，并回到 Primary Side Bar。
    ads::CDockAreaWidget *leftArea = m_lastArea.value(Region::PrimarySidebar, nullptr);
    ads::CDockAreaWidget *newArea = nullptr;
    if (leftArea)
        newArea = m_dock->addDockWidgetTabToArea(dock, leftArea);
    else
        newArea = m_dock->addDockWidget(ads::LeftDockWidgetArea, dock);
    if (newArea)
    {
        newArea->setAllowedAreas(ads::LeftDockWidgetArea | ads::RightDockWidgetArea |
                                  ads::BottomDockWidgetArea);
        m_lastArea.insert(Region::PrimarySidebar, newArea);
        if (!m_panels.contains(Region::PrimarySidebar))
            m_panels.insert(Region::PrimarySidebar, dock);
    }

    for (auto it = m_docksByArea.begin(); it != m_docksByArea.end(); ++it)
        it.value().removeAll(dock);
    m_docksByArea[Region::PrimarySidebar].append(dock);
    for (auto it = m_panels.begin(); it != m_panels.end(); ++it)
    {
        if (it.value() != dock || it.key() == Region::PrimarySidebar)
            continue;
        const QList<ads::CDockWidget *> rest = m_docksByArea.value(it.key());
        it.value() = rest.isEmpty() ? nullptr : rest.first();
    }
    m_panels.remove(Region::PrimarySidebar);
    m_panels.insert(Region::PrimarySidebar, dock);
    if (!m_leftPageKeys.contains(pageKey))
        m_leftPageKeys << pageKey;
    m_collapsedViews.remove(pageKey);

    m_activeActivity = target;
    m_activityBar->setActiveActivity(target);
    dock->toggleView(true);
    activateDockContent(dock);
    if (created)
        rebuildActivityBar();
    else
        syncActivityViews(target);
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

void ShellBinder::applyTheme(bool dark, const QString &ribbonQss, const QString &dockQss)
{
    Q_UNUSED(dark);
    // SARibbon 自绘，全局 QSS 压不住它的内建配色。把 token 化的覆盖 QSS
    // 直接设给 ribbonBar 本身——颜色由 ThemeManager 生成，这里只负责套用
    // （守 §7：绑定器是唯一知道 SARibbon 的地方）。
    if (m_ribbon)
        m_ribbon->setStyleSheet(ribbonQss);
    // ADS 页签条同理：它自带 linux 浅色 palette 规则，且旧版给每个页签塞的
    // 内联 transparent 把 active 渐变也抹平了（四组外观分裂、看不出选中）。
    // 这里以 ADS 内建表为底、叠 token 化的停靠区样式，四组统一。
    //
    // **必须缓存 dockQss**：MainWindow 是在构造早期发 themeChanged 的，那时
    // 绑定器还没建 CDockManager（ensureDockManager 在首个面板请求时才建），
    // 于是 m_dock 为空、样式被静默跳过——页签继续走 ADS 的 active 渐变
    // （palette(window)→palette(light)），看起来就是"每组选中页签一块深灰"。
    // 缓存后由 ensureDockManager 建完立刻补应用一次。
    m_dockQss = dockQss;
    if (m_dock && !dockQss.isEmpty())
        m_dock->setStyleSheet(m_adsBaseQss + dockQss);
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

void ShellBinder::setViewTitleActionProvider(
    std::function<QList<QAction *>(QWidget *parent, const QString &viewId)> provider)
{
    m_viewTitleProvider = std::move(provider);
}

int ShellBinder::viewTitleActionCount(const QString &viewId) const
{
    return m_viewTitleActions.value(viewId).size();
}

void ShellBinder::setWhenEvaluator(std::function<bool(const QString &)> evaluator)
{
    m_whenEval = std::move(evaluator);
}

void ShellBinder::applyWhenVisibility()
{
    // 只有可见集合真的变了才重建：上下文每改一个键都重建 ribbon 会闪。
    if (!m_ribbon)
        return;
    if (visibleRibbonPageLabels() == m_lastVisiblePages)
        return;
    rebuildRibbon();
}

QStringList ShellBinder::visibleRibbonPageLabels() const
{
    QStringList out;
    for (const nextpilot::plotscope::registry::UiSlot &slot :
         (m_slots ? m_slots->slotsOfKind(nextpilot::plotscope::registry::SlotKind::RibbonPage)
                  : QList<nextpilot::plotscope::registry::UiSlot>{}))
    {
        if (slot.when.isEmpty() || !m_whenEval || m_whenEval(slot.when))
            out << slot.label;
    }
    return out;
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
    QStringList visiblePages;
    const auto whenVisible = [this](const QString &when) {
        // 空表达式=恒真；没有求值器时一律可见（headless/测试环境不崩）。
        return when.isEmpty() || !m_whenEval || m_whenEval(when);
    };
    for (const nextpilot::plotscope::registry::UiSlot &pageSlot :
         m_slots ? m_slots->slotsOfKind(nextpilot::plotscope::registry::SlotKind::RibbonPage)
                 : QList<nextpilot::plotscope::registry::UiSlot>{})
    {
        if (!whenVisible(pageSlot.when))
            continue;
        visiblePages << pageSlot.label;
        SARibbonCategory *category = m_ribbon->addCategoryPage(pageSlot.label);
        if (!category)
            continue;
        built << category;

        for (const nextpilot::plotscope::registry::UiSlot &groupSlot :
             m_slots ? m_slots->childrenOf(pageSlot.slotId)
                     : QList<nextpilot::plotscope::registry::UiSlot>{})
        {
            if (groupSlot.kind != nextpilot::plotscope::registry::SlotKind::RibbonGroup)
                continue;
            if (!whenVisible(groupSlot.when))
                continue;
            const auto actions =
                m_slots ? m_slots->actionsIn(groupSlot.slotId)
                        : QList<nextpilot::plotscope::registry::BoundUiAction>{};
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

                // 动作级可见性：先按页/组过滤，再按 when 求值（求值走命令层注入的
                // 求值器，binder 不解析表达式；空 when = 恒真）。
                if (!whenVisible(ua.when))
                    continue;

                // 图标解析（A2c 呈现侧三件）：先查 IconRegistry（"plot/<kind>" 缩略图，
                // plot 呈现层装配时注册）；未命中退回主题图标名。壳不持有绘制器，
                // 不再 include PlotThumbnails（已归位 modules/plot/gui）。
                QIcon ico = gui::IconRegistry::instance().icon(ua.icon);
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
                // 语义型图库（分析应用 / 数据来源）条目图标相似，必须带文字才能分辨；
                // 图型库靠缩略图本身已可辨认，保留 IconOnly 避免文字挤占缩略图。
                const bool semanticGallery =
                    groupSlot.slotId == QLatin1String("ribbon.group.analysis.apps")
                    || groupSlot.slotId == QLatin1String("ribbon.group.data.sources");
                for (const QString &g : galleryOrder)
                {
                    auto *grp = gallery->addCategoryActions(g, galleryActions.value(g));
                    grp->setGalleryGroupStyle(semanticGallery
                                                  ? SARibbonGalleryGroup::IconWithText
                                                  : SARibbonGalleryGroup::IconOnly);
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

    // 记下本次实际建出来的页集合：applyWhenVisibility 靠它判断"要不要重建"。
    m_lastVisiblePages = visiblePages;
}

void ShellBinder::dock(Region region, const QString &title, QWidget *content,
                       const QString &icon, const QString &pageKey)
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

    // 诊断并入底部面板组（Region::Panel）：归一化在边界完成，进程内只传枚举。

    // 左栏不再是一个承载 section 栈的 dock：数据源、脚本文件等各自是独立
    // CDockWidget，才能像 VS Code 一样跨左/右/底拖动。
    auto *dock = new ads::CDockWidget(m_dock, title);
    // objectName 必须唯一：dockWidgetsMap/findDockWidget 按名索引，同区多面板
    // 撞名会让后面的覆盖前面的（"脚本调试"曾被"属性"顶掉）。加序号消歧。
    dock->setObjectName(QStringLiteral("ps.dock.%1.%2").arg(regionName(region)).arg(m_dockSeq++));
    // 视图标题栏动作（VS Code 的 view/title）：只认面板类型 id，不看区域名——
    // 模块在哪个区开面板不改变"这是哪个视图"的语义。
    if (m_viewTitleProvider && !pageKey.isEmpty())
    {
        QList<QAction *> actions = m_viewTitleProvider(dock, pageKey);
        if (!actions.isEmpty())
        {
            dock->setTitleBarActions(actions);
            m_viewTitleActions.insert(pageKey, actions);
        }
    }
    m_dockByWidget.insert(content, dock);
    m_docksByArea[region].append(dock); // 该区页签序列（panelTitles 断言/诊断用）
    if (region == Region::PrimarySidebar && !pageKey.isEmpty()
        && !m_leftPageKeys.contains(pageKey))
        m_leftPageKeys << pageKey;
    if (!pageKey.isEmpty())
        m_dockByKey.insert(pageKey, dock); // 视图页逐页开关按钮的定位表
    // 标题栏图标：先认面板自己声明的图标（同一侧栏并列多个面板时才分得开），
    // 没有声明才退回该区域的默认图标。
    static const QHash<Region, const char *> kDockIcons = {
        {Region::PrimarySidebar, "data_table"},
        {Region::SecondarySidebar, "panel"},
        {Region::Panel, "history"}};
    QString iconName = icon;
    if (iconName.isEmpty())
    {
        const auto it = kDockIcons.constFind(region);
        if (it != kDockIcons.constEnd())
            iconName = QString::fromLatin1(it.value());
    }
    dock->setIcon(iconFor(iconName));
    // 标题文字不再带一块异色底——但**不写内联样式**：页签外观统一交给
    // ThemeManager 的 dockStyleSheet()（token 化、四组一致）。内联样式优先级
    // 高于 ADS 的 activeTab 选择器，写一条 `background: transparent` 就会把
    // 选中底色与渐变一起抹平，四组页签从此看不出当前是哪一个（历史缺陷）。
    if (auto *tab = dock->tabWidget())
    {
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
    }
    dock->setWidget(content);
    applyViewDockFeatures(dock);

    // 底部面板不参与横向缩放：它只挂在编辑器区下方，宽度跟着中央区走，只约束高度。
    if (region == Region::Panel)
    {
        content->setMinimumHeight(120);
    }
    else
    {
        content->setMinimumWidth(170);
        content->setMaximumWidth(520);
    }

    // 同侧第二个及以后的面板并成标签页，不向下堆叠。
    // firstOfArea = 该侧第一个面板：只有它负责"抢占初始焦点"。
    // 后续面板只加进 tab，不抢当前显示——否则启动后显示哪个面板取决于装配顺序
    // （谁最后装配谁露脸），注册顺序一变（例如新增扩展包改了链接顺序）默认面板就跟着变。
    const bool firstOfArea = !m_lastArea.contains(region);
    ads::CDockAreaWidget *dockArea = nullptr;
    if (m_lastArea.contains(region))
    {
        dockArea = m_dock->addDockWidgetTabToArea(dock, m_lastArea.value(region));
    }
    else
    {
        // 底部面板挂在**编辑器区下方**（VS Code 式）：带参照 area 分割，底部宽度
        // 天然等于中央区宽度，左右侧栏保持通栏全高。参照区为空（中央尚未装）时
        // 退回 root 分割器，行为与旧版一致。
        ads::CDockAreaWidget *refArea = nullptr;
        if (region == Region::Panel && m_centralDock)
            refArea = m_centralDock->dockAreaWidget();
        dockArea = m_dock->addDockWidget(adsAreaFor(region), dock, refArea);
        m_lastArea.insert(region, dockArea);
        if (!m_panels.contains(region))
            m_panels.insert(region, dock);
    }
    // 新面板一律前置，并且**显式同步 tab 与内容**：
    // ADS 的 addDockWidgetTabToArea 只保证"加进去了"，不保证 tab 高亮跟着走；
    // 实测出现过"tab 高亮在『脚本文件』、内容区却是『数据源』"的错位
    // （点一下 tab 才对齐），这里由绑定器统一收口，不依赖 ADS 默认行为。
    if (dockArea)
    {
        // 视图只允许在左/右/底三槽之间移动，不能停进 Editor 区或顶部。
        dockArea->setAllowedAreas(ads::LeftDockWidgetArea | ads::RightDockWidgetArea |
                                  ads::BottomDockWidgetArea);
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

    if (region == Region::PrimarySidebar)
    {
        if (!m_sideBarVisible)
            dock->toggleView(false);
        else if (!m_activeActivity.isEmpty())
            syncActivityViews(m_activeActivity);
    }
}

void ShellBinder::setCentralContent(QWidget *content, const QString &title, const QString &icon)
{
    if (!content)
        return;
    ensureDockManager();

    if (!m_centralInstalled)
        installCentral();

    if (m_centralContent)
    {
        // 中央区已有真实内容（画布已就位）：后来者并成**页签**，与画布平级。
        // 典型场景：脚本编辑器请求 Editor 区——写代码需要全宽，
        // 挤在侧栏（400px 上限）里没法用。ADS 页签机制现成，零新布局代码。
        auto *dock = new ads::CDockWidget(m_dock, title.isEmpty() ? QStringLiteral("视图") : title);
        dock->setObjectName(QStringLiteral("ps.dock.central.tab.%1").arg(m_dockSeq++));
        // 中央页签同样按面板声明给图标，空则留空（画布本体已有自己的图标语义）。
        if (!icon.isEmpty())
            dock->setIcon(iconFor(icon));
        m_dockByWidget.insert(content, dock);
        content->setMinimumWidth(320);
        dock->setWidget(content);
        dock->setMinimumWidth(320);
        // 中央页签可关闭，但**不能拖出编辑器区**：编辑器是壳层固定主体，
        // 不是可移动 dock；否则脚本页签能被拖进侧栏，中央区结构失守。
        dock->setFeature(ads::CDockWidget::DockWidgetClosable, true);
        dock->setFeature(ads::CDockWidget::DockWidgetMovable, false);
        dock->setFeature(ads::CDockWidget::DockWidgetFloatable, false);
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
    if (!m_dock || !m_centralDock)
        return;
    ads::CDockAreaWidget *centralArea = m_centralDock->dockAreaWidget();
    if (!centralArea)
        return;

    // ---- 1) 纵向：编辑器 | 底部面板 ----
    // 底部面板挂在中央区下方后，中央 area 的 parentSplitter 就是这条纵向分割器。
    // 左右侧栏不在这条链上，所以它们天然通栏全高。
    if (ads::CDockSplitter *vsp = centralArea->parentSplitter())
    {
        if (vsp->count() == 2)
        {
            const QList<int> vs = vsp->sizes();
            int totalH = 0;
            for (int v : vs)
                totalH += v;
            if (totalH >= 400) // 窗口还没真正布局出来时别硬塞
            {
                QWidget *centerChild = childOfSplitter(centralArea, vsp);
                QWidget *bottomChild = nullptr;
                if (ads::CDockWidget *bd = m_panels.value(Region::Panel))
                    if (ads::CDockAreaWidget *ba = bd->dockAreaWidget())
                        bottomChild = childOfSplitter(ba, vsp);
                const int ci = centerChild ? vsp->indexOf(centerChild) : -1;
                const int bi = bottomChild ? vsp->indexOf(bottomChild) : -1;
                if (ci >= 0 && bi >= 0 && ci != bi)
                {
                    const int bottomPx = qBound(120, int(totalH * 0.28), qMax(120, totalH - 300));
                    QList<int> want = vs;
                    want[bi] = bottomPx;
                    want[ci] = qMax(300, totalH - bottomPx);
                    vsp->setSizes(want);
                }
            }
        }
    }

    // ---- 2) 横向：左侧 256 | 编辑器 | 右侧 300 ----
    // 从侧栏 area 取 root 水平分割器：中央 area 的 parentSplitter 此时已是纵向，
    // 不能再用它算左右宽度（旧实现就是这么错的）。
    ads::CDockWidget *sideDock = m_panels.value(Region::PrimarySidebar);
    if (!sideDock)
        sideDock = m_panels.value(Region::SecondarySidebar);
    ads::CDockAreaWidget *sideArea = sideDock ? sideDock->dockAreaWidget() : nullptr;
    ads::CDockSplitter *hsp = sideArea ? sideArea->parentSplitter() : nullptr;
    if (!hsp)
        return;
    const QList<int> sizes = hsp->sizes();
    if (sizes.size() < 2)
        return;
    int total = 0;
    for (int v : sizes)
        total += v;
    if (total < 800)
        return; // 窗口还没真正布局出来

    QWidget *centerChild = childOfSplitter(centralArea, hsp);
    if (!centerChild)
        return;
    const int ci = hsp->indexOf(centerChild);
    if (ci < 0)
        return;

    // 左侧 256px（VS Code 侧栏默认宽度）、右侧 300px；余下全部归中央。
    QList<int> want = sizes;
    int sides = 0;
    for (int i = 0; i < sizes.size(); ++i)
    {
        if (i == ci)
            continue;
        want[i] = (i < ci) ? qMin(sizes.at(i), 256) : qMin(sizes.at(i), 300);
        sides += want[i];
    }
    want[ci] = qMax(480, total - sides);
    hsp->setSizes(want);
}

void ShellBinder::installCentral()
{
    if (!m_dock || m_centralInstalled)
        return;

    // 中央区内容此刻可能还没到（画布模块可能尚未 attach）：
    // 先用一个占位容器占住"中央"这个位置，
    // 保证后续所有侧边 dock 都排在它之后——顺序约束由外壳兜住。
    // 占位是居中提示文字（空态），不是一块死灰面板；内容到达后由
    // setCentralContent() 取走并 deleteLater。
    QWidget *content;
    if (m_centralContent)
    {
        content = m_centralContent;
    }
    else
    {
        auto *placeholder = new QLabel(QStringLiteral("暂无打开的视图"));
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setObjectName(QStringLiteral("ps.editor.placeholder"));
        auto *placeholderLayout = new QHBoxLayout(placeholder);
        placeholderLayout->setContentsMargins(24, 24, 24, 24);
        content = placeholder;
    }
    m_centralDock = new ads::CDockWidget(m_dock, QStringLiteral("主视图"));
    m_centralDock->setObjectName(QStringLiteral("ps.dock.editor"));
    content->setMinimumWidth(320);
    // 编辑器区高度下限：底部面板可拖高，但不能把画布挤成一条缝。
    content->setMinimumHeight(300);
    m_centralDock->setWidget(content);
    m_centralDock->setMinimumWidth(320);
    m_centralDock->setMinimumHeight(300);
    m_centralDock->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    m_centralDock->setFeature(ads::CDockWidget::DockWidgetMovable, false);
    m_centralDock->setFeature(ads::CDockWidget::DockWidgetFloatable, false);

    // 用 addDockWidget(CenterDockWidgetArea) 而不是 setCentralWidget()：
    // 后者是 ADS 的"独占中央控件"入口，要求它是唯一且第一个 dock，
    // 且会把中央控件排除在普通 dock 体系之外——与"画布区是一个可停靠区域"
    // 的语义不符。故使用 addDockWidget(CenterDockWidgetArea, ...)。
    auto *area = m_dock->addDockWidget(ads::CenterDockWidgetArea, m_centralDock);
    if (area)
    {
        area->setDockAreaFlag(ads::CDockAreaWidget::HideSingleWidgetTitleBar, true);
        // Editor 区不接受任何视图停靠；中央页签自身也不可移动。
        area->setAllowedAreas(ads::NoDockWidgetArea);
    }
    m_centralInstalled = true;
}

bool ShellBinder::setDockPageVisible(const QString &pageKey, bool visible)
{
    ads::CDockWidget *dock = m_dockByKey.value(pageKey, nullptr);
    if (!dock)
        return false;
    // 旧布局可能把面板留成浮动窗口；打开时先收回所属停靠区，再切显隐。
    if (visible && dock->isFloating())
    {
        for (auto it = m_docksByArea.constBegin(); it != m_docksByArea.constEnd(); ++it)
        {
            if (it.value().contains(dock))
            {
                m_dock->addDockWidget(adsAreaFor(it.key()), dock);
                break;
            }
        }
    }
    if (visible && isLeftPage(pageKey))
    {
        const QString activity = activityForPanel(pageKey);
        if (!activity.isEmpty() && activity != m_activeActivity)
            activateActivity(activity);
    }
    // 只动这一个页签：同区其它页签（以及所在 dock 区本身）不受影响。
    dock->toggleView(visible);
    if (visible)
        activateDockContent(dock);
    return true;
}

bool ShellBinder::isDockPageVisible(const QString &pageKey) const
{
    ads::CDockWidget *dock = m_dockByKey.value(pageKey, nullptr);
    // 单页口径：页签未关闭即"显示"。不用 isVisible()——ADS 只对当前页签
    // 返回 true，页签组里非当前页会被误判成隐藏。
    return dock && !dock->isClosed();
}

QString ShellBinder::dockPageTitle(const QString &pageKey) const
{
    ads::CDockWidget *dock = m_dockByKey.value(pageKey, nullptr);
    return dock ? dock->windowTitle() : QString();
}

QWidget *ShellBinder::panelWidget(Region region) const
{
    ads::CDockWidget *dock = m_panels.value(region, nullptr);
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

bool ShellBinder::focusPanelByTitle(Region region, const QString &title)
{
    for (ads::CDockWidget *dw : m_docksByArea.value(region))
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

QList<QWidget *> ShellBinder::panelWidgets(Region region) const
{
    ads::CDockWidget *first = m_panels.value(region, nullptr);
    if (!first)
        return {};
    QList<QWidget *> out;
    ads::CDockAreaWidget *dockAreaWidget = first->dockAreaWidget();
    if (!dockAreaWidget)
    {
        out.append(first->widget());
        return out;
    }
    const QList<ads::CDockWidget *> docks = dockAreaWidget->dockWidgets();
    for (ads::CDockWidget *dw : docks)
        out.append(dw->widget());
    return out;
}

QWidget *ShellBinder::dockHost()
{
    // 区域宿主 = ADS dock 容器本体（懒建；与 dock()/setCentralContent 同一容器）。
    // dock 视图/中央编辑器的控件先生在这里，随后由 dock()/setCentralContent()
    // 经 ADS 接管——不会出现"挂在 QMainWindow 上先浮窗再回抱"的窗口闪烁。
    ensureDockManager();
    return m_dock;
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

QString ShellBinder::panelTitle(Region region) const
{
    ads::CDockWidget *dock = m_panels.value(region, nullptr);
    return dock ? dock->windowTitle() : QString();
}

QStringList ShellBinder::panelTitles(Region region) const
{
    QStringList out;
    for (ads::CDockWidget *dw : m_docksByArea.value(region))
        out << dw->windowTitle();
    return out;
}

QString ShellBinder::activePanelTitle(Region region) const
{
    const QList<ads::CDockWidget *> docks = m_docksByArea.value(region);
    if (docks.isEmpty() || !docks.first())
        return QString();
    ads::CDockAreaWidget *dockArea = docks.first()->dockAreaWidget();
    if (!dockArea)
        return QString();
    ads::CDockWidget *cur = dockArea->currentDockWidget();
    return cur ? cur->windowTitle() : QString();
}

QString ShellBinder::visiblePanelTitle(Region region) const
{
    for (ads::CDockWidget *dw : m_docksByArea.value(region))
    {
        if (!dw)
            continue;
        QWidget *content = dw->widget();
        if (content && content->isVisible())
            return dw->windowTitle();
    }
    return QString();
}

QStringList ShellBinder::contentsOrder(Region region) const
{
    QStringList out;
    const QList<ads::CDockWidget *> docks = m_docksByArea.value(region);
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

bool ShellBinder::triggerAction(const QString &actionId)
{
    QAction *action = m_actionById.value(actionId, nullptr);
    if (!action)
        return false;
    action->trigger();
    return true;
}

bool ShellBinder::dispatchAction(const QString &actionId, const QString &intentOp,
                                 const QString &data)
{
    if (triggerAction(actionId))
        return true;
    if (!m_dispatch || intentOp.isEmpty())
        return false;
    m_dispatch(actionId, intentOp, data);
    return true;
}

bool ShellBinder::bottomPanelVisible() const
{
    ads::CDockWidget *dock = m_panels.value(Region::Panel);
    if (!dock)
        return false;
    if (ads::CDockAreaWidget *area = dock->dockAreaWidget())
        return area->isVisible();
    return dock->isVisible();
}

bool ShellBinder::toggleBottomPanel()
{
    ads::CDockWidget *dock = m_panels.value(Region::Panel);
    if (!dock)
        return false;
    if (ads::CDockAreaWidget *area = dock->dockAreaWidget())
    {
        area->setVisible(!area->isVisible());
        return true;
    }
    return false;
}

bool ShellBinder::auxZoneVisible() const
{
    ads::CDockWidget *dock = m_panels.value(Region::SecondarySidebar);
    if (!dock)
        return false;
    if (ads::CDockAreaWidget *area = dock->dockAreaWidget())
        return area->isVisible();
    return dock->isVisible();
}

bool ShellBinder::toggleAuxZone()
{
    ads::CDockWidget *dock = m_panels.value(Region::SecondarySidebar);
    if (!dock)
        return false;
    if (ads::CDockAreaWidget *area = dock->dockAreaWidget())
    {
        area->setVisible(!area->isVisible());
        return true;
    }
    return false;
}

int ShellBinder::currentCentralIndex() const
{
    if (!m_centralDock || !m_centralDock->dockAreaWidget())
        return -1;
    return m_centralDock->dockAreaWidget()->currentIndex();
}

bool ShellBinder::activateCentralIndex(int index)
{
    if (!m_centralDock || !m_centralDock->dockAreaWidget())
        return false;
    ads::CDockAreaWidget *area = m_centralDock->dockAreaWidget();
    if (index < 0 || index >= area->dockWidgets().size())
        return false;
    area->setCurrentIndex(index);
    return true;
}

QString ShellBinder::currentCentralTitle() const
{
    if (!m_centralDock || !m_centralDock->dockAreaWidget())
        return QString();
    ads::CDockWidget *current = m_centralDock->dockAreaWidget()->currentDockWidget();
    return current ? current->windowTitle() : QString();
}

bool ShellBinder::closeCurrentEditor()
{
    if (!m_centralDock || !m_centralDock->dockAreaWidget())
        return false;
    ads::CDockAreaWidget *area = m_centralDock->dockAreaWidget();
    ads::CDockWidget *current = area->currentDockWidget();
    if (!current || current == m_centralDock)
        return false;
    current->closeDockWidget();
    return true;
}

void ShellBinder::setActivityBadge(const QString &activityId, int count)
{
    if (m_activityBar)
        m_activityBar->setBadge(activityId, count);
}

int ShellBinder::activityBadge(const QString &activityId) const
{
    return m_activityBar ? m_activityBar->badge(activityId) : 0;
}

bool ShellBinder::setActiveActivity(const QString &activityId)
{
    if (!m_slots || !m_slots->activity(activityId))
        return false;
    activateActivity(activityId);
    return true;
}

QStringList ShellBinder::activityIds() const
{
    return m_activityBar ? m_activityBar->activityIds() : QStringList();
}

bool ShellBinder::pinViewToActivity(const QString &pageKey, const QString &activityId)
{
    ads::CDockWidget *dock = m_dockByKey.value(pageKey, nullptr);
    if (!dock)
        return false;
    if (!activityId.isEmpty() && (!m_slots || !m_slots->activity(activityId)))
        return false;
    pinDockToActivity(dock, activityId);
    return true;
}

void ShellBinder::setActivityOrder(const QStringList &order)
{
    m_activityOrder = order;
    rebuildActivityBar();
}

void ShellBinder::setActivityBarVisible(bool visible)
{
    m_activityBarVisible = visible;
    if (m_activityBar)
        m_activityBar->setVisible(visible);
}

bool ShellBinder::activityBarVisible() const
{
    return m_activityBarVisible;
}

QWidget *ShellBinder::activityBarWidget() const
{
    return m_activityBar;
}

void ShellBinder::setSideBarVisible(bool visible)
{
    m_sideBarVisible = visible;
    for (ads::CDockWidget *dock : leftDocks())
        dock->toggleView(visible);
}

bool ShellBinder::sideBarVisible() const
{
    return m_sideBarVisible;
}

bool ShellBinder::toggleSideBar()
{
    setSideBarVisible(!m_sideBarVisible);
    return m_sideBarVisible;
}

int ShellBinder::sideBarWidth() const
{
    for (ads::CDockWidget *dock : leftDocks())
        if (dock->dockAreaWidget())
            return dock->dockAreaWidget()->width();
    return m_sideBarWidth;
}

void ShellBinder::setSideBarWidth(int width)
{
    m_sideBarWidth = qMax(170, width);
}

int ShellBinder::sideBarSectionCount() const
{
    return m_leftPageKeys.size();
}

QStringList ShellBinder::sideBarSectionIds() const
{
    return m_leftPageKeys;
}

QWidget *ShellBinder::sideBarSectionWidget(const QString &id) const
{
    ads::CDockWidget *dock = m_dockByKey.value(id, nullptr);
    return dock ? dock->widget() : nullptr;
}

QString ShellBinder::sideBarSectionActivity(const QString &id) const
{
    return isLeftPage(id) ? activityForPanel(id) : QString();
}

bool ShellBinder::setSideBarSectionExpanded(const QString &id, bool expanded)
{
    ads::CDockWidget *dock = m_dockByKey.value(id, nullptr);
    if (!dock || !isLeftPage(id))
        return false;
    if (expanded)
    {
        m_collapsedViews.remove(id);
        dock->toggleView(true);
    }
    else
    {
        m_collapsedViews.insert(id);
        dock->toggleView(false);
    }
    return true;
}

bool ShellBinder::isSideBarSectionExpanded(const QString &id) const
{
    return m_dockByKey.contains(id) && !m_collapsedViews.contains(id);
}

bool ShellBinder::setRibbonPage(const QString &area)
{
    if (!m_ribbon)
        return false;
    const QList<UiSlot> pages =
        m_slots ? m_slots->slotsOfKind(SlotKind::RibbonPage) : QList<UiSlot>{};
    for (int i = 0; i < pages.size(); ++i)
    {
        if (pages.at(i).slotId != area)
            continue;
        if (i >= m_ribbon->categoryPages().size())
            return false;
        m_ribbon->setCurrentIndex(i);
        m_currentPage = m_ribbon->currentIndex();
        return m_currentPage == i;
    }
    return false;
}

QString ShellBinder::currentRibbonPageArea() const
{
    if (!m_ribbon)
        return QString();
    const QList<UiSlot> pages =
        m_slots ? m_slots->slotsOfKind(SlotKind::RibbonPage) : QList<UiSlot>{};
    const int index = m_ribbon->currentIndex();
    return index >= 0 && index < pages.size() ? pages.at(index).slotId : QString();
}

QJsonObject ShellBinder::captureState() const
{
    // 声明式快照：只描述"结果长什么样"，不含任何操作序列。
    // 于是恢复时缺失的部分可以安全降级（数据没打开、扩展没装上……）。
    QJsonObject state;
    state.insert(QStringLiteral("layoutVersion"), kDockLayoutVersion);
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
    // ribbon 当前页签：读控件真实下标，不读缓存字段——用户点页签时没有回写
    // m_currentPage 的钩子，缓存值会和实际显示脱节，存下去就是假状态。
    state.insert(QStringLiteral("ribbonPage"), m_ribbon ? m_ribbon->currentIndex() : m_currentPage);
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

    // ADS 布局：缺失或版本不符时安全降级（保持当前默认布局）。
    // W1b 改变了左栏 dock 树，旧快照必须跳过，否则会恢复出没有数据源入口的布局。
    const int layoutVersion = state.value(QStringLiteral("layoutVersion")).toInt(0);
    const QString ads = state.value(QStringLiteral("adsLayout")).toString();
    if (layoutVersion >= kDockLayoutVersion && !ads.isEmpty() && m_dock)
        m_dock->restoreState(QByteArray::fromBase64(ads.toLatin1()));

    // ribbon 页签：越界则忽略（安全降级）。这里是构造期，页签栏尚未就位，
    // 当场切换会静默失败——只登记，等首次 showEvent 再切（见 applyPendingRibbonPage）。
    const int page = state.value(QStringLiteral("ribbonPage")).toInt(-1);
    if (page >= 0 && m_ribbon)
        m_pendingRibbonPage = page;
    return true;
}

bool ShellBinder::applyPendingRibbonPage()
{
    if (m_pendingRibbonPage < 0 || !m_ribbon)
        return false;
    const int page = m_pendingRibbonPage;
    // 越界说明那一页这次没装配出来（扩展未装），安全降级：放弃，不再反复尝试。
    if (page >= m_ribbon->categoryPages().size())
    {
        m_pendingRibbonPage = -1;
        return false;
    }
    m_ribbon->setCurrentIndex(page);
    // 只有真的切过去了才更新记录，否则保持实际页——避免把目标值当现状写进快照。
    // 切不成就保留待恢复标记，交给下一次调用（showEvent 里还有一次 300ms 兜底）。
    const int actual = m_ribbon->currentIndex();
    m_currentPage = actual;
    if (actual == page)
        m_pendingRibbonPage = -1;
    return actual == page;
}

QByteArray ShellBinder::saveLayout() const
{
    return m_dock ? m_dock->saveState() : QByteArray();
}

bool ShellBinder::restoreLayout(const QByteArray &state)
{
    return m_dock ? m_dock->restoreState(state) : false;
}

namespace
{
QJsonObject rectInWindow(QWidget *widget, QWidget *window)
{
    QJsonObject out;
    if (!widget || !window)
        return out;
    const QPoint origin = widget->mapTo(window, QPoint(0, 0));
    out.insert(QStringLiteral("x"), origin.x());
    out.insert(QStringLiteral("y"), origin.y());
    out.insert(QStringLiteral("w"), widget->width());
    out.insert(QStringLiteral("h"), widget->height());
    return out;
}
} // namespace

QJsonObject ShellBinder::geometrySnapshot() const
{
    QJsonObject out;
    if (!m_window)
        return out;

    // 中央区单独取：它是左右分栏与上下分栏的交点。
    if (m_centralDock)
        if (auto *area = m_centralDock->dockAreaWidget())
            out.insert(QStringLiteral("editor"), rectInWindow(area, m_window));

    for (const Region region : {Region::PrimarySidebar, Region::SecondarySidebar,
                                Region::Panel})
    {
        ads::CDockWidget *dock = m_panels.value(region);
        if (!dock)
            continue;
        if (auto *dockArea = dock->dockAreaWidget())
            out.insert(regionName(region), rectInWindow(dockArea, m_window));
    }

    if (auto *status = m_window->statusBar())
        out.insert(QStringLiteral("statusBar"), rectInWindow(status, m_window));
    return out;
}

} // namespace nextpilot::plotscope::gui
