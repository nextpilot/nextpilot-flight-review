#include "GuiContext.h"

#include "PanelFactoryHub.h"
#include "ShellBinder.h"

#include "AppContext.h"

#include <algorithm>

#include <QFileDialog>
#include <QMainWindow>
#include <QDateTime>
#include <QSet>

#include "ErrorCodes.h"
using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::context;
using namespace nextpilot::plotscope::host;

namespace nextpilot::plotscope::gui
{

GuiContext::GuiContext(QMainWindow *window, workbench::ISlotsService *slotSvc, ShellBinder *binder, AppContext &appCtx,
                       IIntentExecutor *executor, const workbench::IPanelsService *panelsService,
                       workbench::IViewsService *viewsService, workbench::IEditorService *editorService,
                       QObject *parent)
    : QObject(parent), m_window(window), m_slots(slotSvc), m_binder(binder), m_appCtx(appCtx), m_executor(executor),
      m_panelsService(panelsService), m_viewsService(viewsService), m_editorService(editorService)
{
    // 状态栏中继：登记/文案/瞬时消息的真源在 AppContext::status()（libs/context），
    // 这里只把 hub 事件转发成壳侧信号，MainWindow 的订阅代码不用动。
    context::StatusHub &hub = m_appCtx.status();
    QObject::connect(&hub, &context::StatusHub::statusChanged, this,
                     [this](const QString &id, const QString &text) {
                         Q_EMIT statusTextChanged(id, text);
                     });
    QObject::connect(&hub, &context::StatusHub::statusSpecDeclared, this,
                     [this](const registry::StatusSpec &spec) { Q_EMIT statusSpecDeclared(spec); });
    QObject::connect(&hub, &context::StatusHub::statusFlashed, this,
                     [this](const QString &text) { Q_EMIT statusFlashed(text); });
}

void GuiContext::dockWidget(QWidget *widget, nextpilot::plotscope::registry::Region region,
                            const QString &title, const QString &pageKey)
{
    if (!widget || !m_binder)
        return;
    // 标题优先级：调用方显式指定（领域面板自己的描述）> 区域规范名兜底。
    const QString resolved = title.isEmpty()
                                 ? nextpilot::plotscope::registry::regionName(region)
                                 : title;
    // pageKey = 面板类型 id：视图页逐页开关按钮按它定位页签（稳定键，
    // 不随显示标题改动而失效）。
    m_binder->dock(region, resolved, widget, m_panelIcon.take(widget), pageKey);
}

void GuiContext::dockCentral(QWidget *widget, const QString &titleOverride)
{
    if (!widget || !m_window)
        return;
    // 中央内容交给 ADS（画布区是不可关闭的中央 dock；后来的内容并成页签）。
    if (m_binder)
        m_binder->setCentralContent(widget, titleOverride, m_panelIcon.take(widget));
    else
        m_window->setCentralWidget(widget);
}

void GuiContext::updateStatus(const QString &id, const QString &text)
{
    // 推模式（D8）：未声明 id 由 hub 忽略；**不做节流**，限频是生产方的事。
    m_appCtx.status().updateStatus(id, text);
}

void GuiContext::flashStatus(const QString &text)
{
    m_appCtx.status().flash(text);
}

void GuiContext::setActivityBadge(const QString &activityId, int count)
{
    if (m_binder)
        m_binder->setActivityBadge(activityId, count);
}

// A8b-M2 裁定 52442006 Step 3：installContributions 整段搬 assembly/ContributionsInstaller
// （写 ISlotsService/StatusHub，壳只渲染）；壳不再灌回，见 ExtensionActivationService.cpp。

QWidget *GuiContext::requestPanel(nextpilot::plotscope::registry::Region region, const QString &panelType)
{
    // 旧工厂入口（A2b 笔2b 第①步复审必修 A 后唯一余留）：仅尚未迁移的调用点使用；
    // ②~⑥步逐 owner 迁到 openView/openEditor（走 IViewsService/IEditorService），
    // ⑦步删本函数，旧工厂全退场。
    // 去重由**工厂登记**决定，不在这里一刀切：
    //   singleton=true（默认）：同一类型已有活着的实例就聚焦它（"再打开"=置前），
    //     键只用 panelType——同类面板只该有一个家，换区请求也是聚焦既有实例。
    //   singleton=false：每次新建（多文档/对比视图）。
    // 页签可关闭（ADS closable），QPointer 保证关闭后缓存自动失效、可重建。
    const bool singleton = nextpilot::plotscope::registry::PanelFactoryHub::singleton(panelType);
    if (singleton)
    {
        if (QWidget *existing = m_panels.value(panelType))
        {
            if (m_binder)
                m_binder->raisePanel(existing);
            return existing;
        }
    }

    // 部件由**视图层自注册**的工厂提供：外壳与模块都不认识具体实现。
    // 通用面板与扩展包领域面板共用同一张工厂表——领域实现（如 car.can-browser）
    // 同样在呈现层 TU 里 PS_REGISTER_PANEL 自注册；工厂未注册即返回 nullptr。
    QWidget *widget = nextpilot::plotscope::registry::PanelFactoryHub::create(panelType, m_appCtx, executor(), m_window);
    if (!widget)
        return nullptr;
    if (singleton)
        m_panels.insert(panelType, widget);

    // 标题与图标：工厂登记优先；工厂缺标题/图标时回退扩展包 PanelSpec 描述
    // （领域面板用自己的 label/icon，如"CAN 报文"），provider 区分来源。
    // 包声明读回经 IPanelsService（裁定 52441633 §三），壳不直连扩展注册表。
    QString title = nextpilot::plotscope::registry::PanelFactoryHub::title(panelType);
    QString iconName = nextpilot::plotscope::registry::PanelFactoryHub::icon(panelType);
    const nextpilot::plotscope::registry::PanelSpec *spec =
        m_panelsService ? m_panelsService->panelSpec(panelType) : nullptr;
    if (spec)
    {
        if (title.isEmpty())
            title = spec->label;
        if (iconName.isEmpty())
            iconName = spec->icon;
    }
    if (!iconName.isEmpty())
        m_panelIcon.insert(widget, iconName);

    // 编辑器区走 setCentralContent（首到者占位，后来者并成页签），其余区域走停靠。
    // 标题用面板自注册的显示名（"脚本调试"/"数据源"…），不能用区域标签——
    // 否则同区多面板全是"属性"/"数据源"，页签没法分辨。
    if (region == nextpilot::plotscope::registry::Region::Editor)
        dockCentral(widget, title);
    else
        dockWidget(widget, region, title, panelType);
    return widget;
}

QWidget *GuiContext::openView(const QString &viewId)
{
    // A2b 笔2b（复审必修 C：不只建、还要摆）：壳把 viewId 解到落区并交给 dock——
    // 落区由 viewsContainers[].area 决定（§6 模块不指定区域），壳不按 id 特判、
    // 不自建控件。复用/创建走 IViewsService（2a 定死语义），摆放走 ShellBinder。
    if (!m_viewsService || viewId.isEmpty())
        return nullptr;
    // 已开复用（dock 簿记，与 requestPanel 的 m_panels 同款）：同一 viewId 已有
    // 活实例 → 置前返回，不重复 dock（页签可关，QPointer 兜底自动失效）。
    if (QWidget *existing = m_openViews.value(viewId))
    {
        if (m_binder)
            m_binder->raisePanel(existing);
        return existing;
    }
    // 落区解析：view → 容器 → area 映射（sidebar/auxiliary/bottom 可停靠；
    // ribbon 页是声明式入口不走打开；未知 area 出诊断不猜）。
    const nextpilot::plotscope::registry::ContributedView view = m_viewsService->viewForId(viewId);
    if (view.owner.isEmpty())
    {
        qWarning() << "openView: 未声明 view, viewId =" << viewId;
        return nullptr;
    }
    const nextpilot::plotscope::registry::ContributedViewContainer container =
        m_viewsService->containerForId(view.container);
    nextpilot::plotscope::registry::Region region;
    if (container.area == QLatin1String("sidebar"))
        region = nextpilot::plotscope::registry::Region::PrimarySidebar;
    else if (container.area == QLatin1String("auxiliary"))
        region = nextpilot::plotscope::registry::Region::SecondarySidebar;
    else if (container.area == QLatin1String("bottom"))
        region = nextpilot::plotscope::registry::Region::Panel;
    else
    {
        qWarning() << "openView: 容器 area 不可停靠或未声明, viewId =" << viewId << "containerId =" << view.container
                   << "area =" << container.area;
        return nullptr;
    }
    // parent = 区域宿主（ADS dock 容器），不用裸 QMainWindow——避免"先浮窗再
    // reparent"的窗口闪烁（复审必修 C 第 2 条）。
    QWidget *parent = m_binder ? m_binder->dockHost() : m_window;
    QWidget *widget = m_viewsService->openView(viewId, m_appCtx, executor(), parent);
    if (!widget)
        return nullptr;
    // 摆放：标题/图标来自声明（壳不猜）；pageKey = viewId（稳定键，视图页
    // 开关按钮按它定位页签）。
    if (m_binder)
        m_binder->dock(region, view.name, widget, view.icon, viewId);
    m_openViews.insert(viewId, widget);
    return widget;
}

QWidget *GuiContext::openEditor(const QString &viewType, const nextpilot::plotscope::registry::ResourceRef &ref)
{
    // A2b 笔2b（复审必修 C：不只建、还要摆）：IEditorService 造控件，壳接进
    // 中央区页签（dockCentral——首到占位、后来并页签）。ref 由 owner 构造、
    // 壳不解析 uri；同一 viewType 不同资源不会按错复用。
    if (!m_editorService || viewType.isEmpty())
        return nullptr;
    QWidget *parent = m_binder ? m_binder->dockHost() : m_window;
    QWidget *widget = m_editorService->openEditor(viewType, ref, m_appCtx, executor(), parent);
    if (!widget)
        return nullptr;
    // 已开置前（A2b 笔2b 第③步：同一 ref 复用同页签——ref 语义归 provider，同
    // ref 返回同 widget；壳对已 dock 过的 widget 只置前，不再并新页签）。
    if (QPointer<QWidget> existing = m_openEditors.value(widget); !existing.isNull())
    {
        if (m_binder)
            m_binder->raisePanel(widget);
        return widget;
    }
    // 页签标题：ref.title（资源名）优先；空则回退编辑器声明的 displayName——
    // 壳只挑显示名，不解析 uri 语义。
    QString title = ref.title;
    if (title.isEmpty())
    {
        for (const nextpilot::plotscope::registry::ContributedCustomEditor &editor : m_editorService->customEditors())
        {
            if (editor.viewType == viewType)
            {
                title = editor.displayName;
                break;
            }
        }
    }
    if (m_binder)
        m_binder->setCentralContent(widget, title);
    m_openEditors.insert(widget, widget);
    return widget;
}

QList<PanelSpec> GuiContext::availableDomainPanels(Domain domain) const
{
    // 领域面板清单来自**扩展包**（panels 表，纯数据）。
    //
    // 模块据此知道"当前业务范围里有哪些面板可用"，而不必写死具体 id——
    // 面板由哪个包提供、叫什么，都由包自己决定。
    QList<PanelSpec> out;
    // 领域过滤：面板归属由**包**的领域决定（条目的 domain 由 mount 写入）。
    // 清单经 IPanelsService 读回（裁定 52441633 §三），壳不直连扩展注册表。
    if (m_panelsService)
    {
        for (const PanelSpec &spec : m_panelsService->panels())
        {
            if (domain != Domain::General && spec.domain != domain)
                continue;
            out.append(spec);
        }
    }
    // 顺序由声明决定：order 升序、同权重按 id 字典序（与包装载顺序解耦）。
    std::stable_sort(out.begin(), out.end(), [](const PanelSpec &a, const PanelSpec &b) {
        return a.order != b.order ? a.order < b.order : a.id < b.id;
    });
    return out;
}

QString GuiContext::pickFilePath(const QString &title, const QString &filter)
{
    if (!m_window)
        return QString();
    // 文件对话框是**外壳**的交互能力：模块层不链接 Qt Widgets，开不了它。
    // 模块只拿到路径，之后照样走 data.load 意图——GUI 与 CLI 同一条路径。
    return QFileDialog::getOpenFileName(m_window, title, QString(), filter);
}

IIntentExecutor &GuiContext::executor()
{
    if (m_executor)
        return *m_executor;
    // 未接宿主时返回恒定失败的执行器，绝不返回悬空引用。
    static class NullExecutor final : public IIntentExecutor
    {
    public:
        Result<IntentResult> execute(const Intent &) override
        {
            return fail(err::Unsupported, QStringLiteral("GUI 未连接宿主执行器"));
        }
    } nullExecutor;
    return nullExecutor;
}

} // namespace nextpilot::plotscope::gui
