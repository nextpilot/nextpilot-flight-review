#include "GuiContext.h"

#include "PanelFactoryRegistry.h"
#include "ShellBinder.h"

#include "AppContext.h"

#include <QMainWindow>

using namespace ng::core;
using namespace ng::host;

namespace ng::gui
{

GuiContext::GuiContext(QMainWindow *window, UiSlotRegistry &slotReg, ShellBinder *binder,
                       AppContext &appCtx, IIntentExecutor *executor, QObject *parent)
    : QObject(parent), m_window(window), m_slots(slotReg), m_binder(binder), m_appCtx(appCtx),
      m_executor(executor)
{}

void GuiContext::placePanel(QWidget *widget, const QString &area)
{
    if (!widget || !m_binder)
        return;
    // 插槽声明的标签作为停靠窗口标题；未声明则用区域名兜底。
    QString title = area;
    if (const UiSlot *slot = m_slots.slot(area))
        title = slot->label;
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

void GuiContext::addAction(const QString &area, const UiAction &action)
{
    // 器官本地登记的入口，provider 记为 "module"，卸载时按 provider 清理。
    m_slots.bindLocal(area, action, QStringLiteral("module"));
    if (m_binder)
        m_binder->rebuildRibbon();
}

QWidget *GuiContext::requestPanel(const QString &area, const QString &panelType)
{
    // 部件由**视图层自注册**的工厂提供：外壳与器官都不认识具体实现。
    QWidget *widget = PanelFactoryRegistry::create(panelType, m_appCtx, executor(), m_window);
    if (!widget)
        return nullptr;

    // 中央画布区走 setCentralContent，其余区域走停靠。
    if (area == QStringLiteral("canvas.center"))
        placeCanvas(widget);
    else
        placePanel(widget, area);
    return widget;
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

} // namespace ng::gui
