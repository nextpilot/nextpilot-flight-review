#include "GuiContext.h"

#include "DockManager.h"
#include "PanelFactoryRegistry.h"
#include "PlotCanvas.h"
#include "RibbonBar.h"

#include <QDockWidget>
#include <QMainWindow>

using namespace ng::core;
using namespace ng::host;

namespace ng::gui
{

GuiContext::GuiContext(QMainWindow *window, UiSlotRegistry &slotReg, RibbonBar *ribbon,
                       DockManager *docks, PlotCanvas *canvas, IIntentExecutor *executor,
                       QObject *parent)
    : QObject(parent), m_window(window), m_slots(slotReg), m_ribbon(ribbon), m_docks(docks),
      m_canvas(canvas), m_executor(executor)
{}

void GuiContext::placePanel(QWidget *widget, const QString &area)
{
    if (!widget)
        return;
    // 插槽声明的标签作为停靠窗口标题；未声明则用区域名兜底。
    QString title = area;
    if (const UiSlot *slot = m_slots.slot(area))
        title = slot->label;
    m_docks->dock(area, title, widget);
}

void GuiContext::placeCanvas(QWidget *widget)
{
    if (!widget || !m_window)
        return;
    if (m_canvas)
        m_canvas->setParent(nullptr);
    m_window->setCentralWidget(widget);
}

void GuiContext::declareSlot(const QString &area, const QString &label, SlotKind kind,
                             const QString &parentArea)
{
    m_slots.declareSlot(area, label, QStringLiteral("module"), kind, parentArea);
    refreshRibbon();
}

void GuiContext::addAction(const QString &area, const UiAction &action)
{
    // 器官本地登记的入口，provider 记为 "module"，卸载时按 provider 清理。
    m_slots.bindLocal(area, action, QStringLiteral("module"));
    refreshRibbon();
}

void GuiContext::refreshRibbon()
{
    if (m_ribbon)
        m_ribbon->rebuild(m_slots);
}

QWidget *GuiContext::requestPanel(const QString &area, const QString &panelType)
{
    // 部件由 app 骨架的工厂表提供；模块只描述类型，不依赖具体实现。
    QWidget *widget = PanelFactoryRegistry::create(panelType, m_window);
    if (!widget)
        return nullptr;
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
