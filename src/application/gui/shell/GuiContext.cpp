#include "GuiContext.h"

#include "PanelFactoryHub.h"
#include "ShellBinder.h"

#include "AppContext.h"
#include "ToolboxHub.h"

#include <algorithm>

#include <QFileDialog>
#include <QMainWindow>

#include "ErrorCodes.h"
using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;
using namespace nextpilot::plotscope::context;
using namespace nextpilot::plotscope::host;

namespace nextpilot::plotscope::gui
{

GuiContext::GuiContext(QMainWindow *window, UiSlotHub &slotReg, ShellBinder *binder,
                       AppContext &appCtx, IIntentExecutor *executor, QObject *parent)
    : QObject(parent), m_window(window), m_slots(slotReg), m_binder(binder), m_appCtx(appCtx),
      m_executor(executor)
{}

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

void GuiContext::declareRibbonPage(const QString &id, const QString &label, int order)
{
    m_slots.declareSlot(id, label, QStringLiteral("module"), SlotKind::RibbonPage,
                        QString(), order);
    if (m_binder)
        m_binder->rebuildRibbon();
}

void GuiContext::declareRibbonGroup(const QString &id, const QString &label,
                                    const QString &pageId, int order)
{
    m_slots.declareSlot(id, label, QStringLiteral("module"), SlotKind::RibbonGroup,
                        pageId, order);
    if (m_binder)
        m_binder->rebuildRibbon();
}

void GuiContext::declareActivity(const ActivitySpec &activity)
{
    ActivitySpec spec = activity;
    if (spec.provider.isEmpty())
        spec.provider = QStringLiteral("module");
    m_slots.declareActivity(spec);
    if (m_binder)
        m_binder->rebuildActivityBar();
}

void GuiContext::setActivityBadge(const QString &activityId, int count)
{
    if (m_binder)
        m_binder->setActivityBadge(activityId, count);
}

void GuiContext::addAction(const UiAction &action)
{
    // 模块本地登记的入口，provider 记为 "module"，卸载时按 provider 清理。
    // 归属插槽取 action.area 单一来源（接口已不再另传 area 参数）。
    m_slots.bindLocal(action.area, action, QStringLiteral("module"));
    if (m_binder)
        m_binder->rebuildRibbon();
}

void GuiContext::bindActionProvider(const QString &area, const QString &provider,
                                    std::function<QList<UiAction>()> providerFn)
{
    m_slots.bindActionProvider(area, provider, std::move(providerFn));
    if (m_binder)
        m_binder->rebuildRibbon();
}

void GuiContext::refreshActions()
{
    if (m_binder)
        m_binder->rebuildRibbon();
}

QWidget *GuiContext::requestPanel(nextpilot::plotscope::registry::Region region,
                                  const QString &panelType)
{
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
    QString title = nextpilot::plotscope::registry::PanelFactoryHub::title(panelType);
    QString iconName = nextpilot::plotscope::registry::PanelFactoryHub::icon(panelType);
    if (const nextpilot::plotscope::registry::PanelSpec *spec =
            m_appCtx.toolbox().panels.find(panelType))
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

QList<PanelSpec> GuiContext::availableDomainPanels(Domain domain) const
{
    // 领域面板清单来自**扩展包**（panels 表，纯数据）。
    //
    // 模块据此知道"当前业务范围里有哪些面板可用"，而不必写死具体 id——
    // 面板由哪个包提供、叫什么，都由包自己决定。
    QList<PanelSpec> out;
    const ToolboxHub &toolbox = m_appCtx.toolbox();
    // 领域过滤：面板归属由**包**的领域决定（条目的 domain 由 mount 写入）。
    for (const PanelSpec &spec : toolbox.panels.values())
    {
        if (domain != Domain::General && spec.domain != domain)
            continue;
        out.append(spec);
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
