#include "GuiContext.h"

#include "PanelFactoryRegistry.h"
#include "ShellBinder.h"

#include "AppContext.h"
#include "ExtensionRegistry.h"

#include <QFileDialog>
#include <QMainWindow>

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

GuiContext::GuiContext(QMainWindow *window, UiSlotRegistry &slotReg, ShellBinder *binder,
                       AppContext &appCtx, IIntentExecutor *executor, QObject *parent)
    : QObject(parent), m_window(window), m_slots(slotReg), m_binder(binder), m_appCtx(appCtx),
      m_executor(executor)
{}

void GuiContext::placePanel(QWidget *widget, const QString &area, const QString &titleOverride)
{
    if (!widget || !m_binder)
        return;
    // 标题优先级：调用方显式指定（领域面板自己的描述）> 插槽标签 > 区域名兜底。
    QString title = titleOverride;
    if (title.isEmpty())
    {
        title = area;
        if (const UiSlot *slot = m_slots.slot(area))
            title = slot->label;
    }
    m_binder->dock(area, title, widget);
}

void GuiContext::placeCanvas(QWidget *widget)
{
    if (!widget || !m_window)
        return;
    // 中央内容交给 ADS（画布区是一个不可关闭/移动的中央 dock）。
    if (m_binder)
        m_binder->setCentralContent(widget);
    else
        m_window->setCentralWidget(widget);
}

void GuiContext::declareSlot(const QString &area, const QString &label, SlotKind kind,
                             const QString &parentArea)
{
    m_slots.declareSlot(area, label, QStringLiteral("module"), kind, parentArea);
    if (m_binder)
        m_binder->rebuildRibbon();
}

void GuiContext::addAction(const UiAction &action)
{
    // 器官本地登记的入口，provider 记为 "module"，卸载时按 provider 清理。
    // 归属插槽取 action.area 单一来源（接口已不再另传 area 参数）。
    m_slots.bindLocal(action.area, action, QStringLiteral("module"));
    if (m_binder)
        m_binder->rebuildRibbon();
}

void GuiContext::addDynamicAction(const UiAction &action, const QString &provider)
{
    // 动态条目挂在调用方给的 provider 下（如 "module:analysis-dynamic"），
    // 与终身有效的 "module" 条目隔离——重置只影响动态组。
    m_slots.bindLocal(action.area, action, provider);
    if (m_binder)
        m_binder->rebuildRibbon();
}

void GuiContext::resetDynamicActions(const QString &provider)
{
    m_slots.unbindProvider(provider);
    if (m_binder)
        m_binder->rebuildRibbon();
}

QWidget *GuiContext::requestPanel(const QString &area, const QString &panelType)
{
    // 部件由**视图层自注册**的工厂提供：外壳与器官都不认识具体实现。
    QWidget *widget = nextpilot::plotscope::registry::PanelFactoryRegistry::create(panelType, m_appCtx, executor(), m_window);
    if (!widget)
        return nullptr;

    // 中央画布区走 setCentralContent，其余区域走停靠。
    if (area == QStringLiteral("canvas.center"))
        placeCanvas(widget);
    else
        placePanel(widget, area);
    return widget;
}

QList<PanelSpec> GuiContext::availableDomainPanels(Domain domain) const
{
    // 领域面板清单来自**知识包**（PanelContribution，纯数据）。
    //
    // 模块据此知道"当前业务范围里有哪些面板可用"，而不必写死具体 id——
    // 面板由哪个包提供、叫什么，都由包自己决定。
    QList<PanelSpec> out;
    const ExtensionRegistry &toolbox = m_appCtx.toolbox();
    for (IToolContribution *c : toolbox.contributions(ContributionKind::Panel))
    {
        auto *panel = dynamic_cast<PanelContribution *>(c);
        if (!panel)
            continue;
        for (const PanelSpec &spec : panel->panels)
        {
            // 领域过滤：面板归属由**贡献者**的领域决定。
            // 用贡献者所在的 pack 领域，而非面板自己声明（面板是包的一部分）。
            const Domain ownerDomain = panel->domain();
            if (domain != Domain::General && ownerDomain != domain)
                continue;
            out.append(spec);
        }
    }
    return out;
}

QWidget *GuiContext::requestDomainPanel(const QString &area, const QString &panelId)
{
    if (panelId.isEmpty())
        return nullptr;
    // 实现来自**知识包的呈现层**（注册进同一张工厂表），
    // 或通用视图层——两者共用一张表，模块不关心是谁提供的。
    QWidget *widget = nextpilot::plotscope::registry::PanelFactoryRegistry::create(panelId, m_appCtx, executor(),
                                                             m_window);
    if (!widget)
        return nullptr;

    // 标题优先用面板自己的描述（知识包声明的"CAN 报文"），
    // 否则退回插槽的 label。
    QString title;
    for (const PanelSpec &spec : availableDomainPanels(Domain::General))
    {
        if (spec.id == panelId)
        {
            title = spec.label;
            break;
        }
    }
    if (title.isEmpty())
        title = area;

    // 中央画布区走 setCentralContent，其余区域走停靠。
    // 把面板自己的描述传给 placePanel 作标题——领域面板的形态（含标题）
    // 由知识包决定，而不是被通用插槽的"属性"覆盖掉。
    if (area == QStringLiteral("canvas.center"))
        placeCanvas(widget);
    else
        placePanel(widget, area, title);
    return widget;
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
            return fail(QStringLiteral("eunsupported"), QStringLiteral("GUI 未连接宿主执行器"));
        }
    } nullExecutor;
    return nullExecutor;
}

} // namespace nextpilot::plotscope::gui
