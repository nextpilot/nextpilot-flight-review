#pragma once

#include "IModule.h"
#include "UiSlotRegistry.h"

#include <QObject>

class QMainWindow;

namespace ng::gui
{

class DockManager;
class PlotCanvas;
class RibbonBar;

// GUI 上下文：宿主传给器官的唯一 UI 接口实现。
//
// 职责单一：把"器官/工具箱想放的东西"路由到对应的 GUI 容器
// （ribbon -> RibbonBar，dock -> DockManager，画布 -> PlotCanvas）。
// 它不认识任何具体业务，也不决定"哪个动作做什么"——动作的语义由
// 器官自己声明（意图 op），执行由宿主统一分派。
class GuiContext final : public QObject, public ng::host::UiContext
{
    Q_OBJECT
public:
    GuiContext(QMainWindow *window, ng::core::UiSlotRegistry &slotReg,
               RibbonBar *ribbon, DockManager *docks, PlotCanvas *canvas,
               ng::core::IIntentExecutor *executor = nullptr,
               QObject *parent = nullptr);

    // ng::host::UiContext
    void placePanel(QWidget *widget, const QString &area) override;
    void placeCanvas(QWidget *widget) override;
    void declareSlot(const QString &area, const QString &label, ng::core::SlotKind kind,
                     const QString &parentArea = QString()) override;
    void addAction(const QString &area, const ng::core::UiAction &action) override;

    // 插槽变化后需要刷新 ribbon（新增/移除动作后调用）。
    void refreshRibbon();

    // 请求 app 骨架提供一个部件，放到指定区域。
    QWidget *requestPanel(const QString &area, const QString &panelType) override;

    // 宿主执行器（模块在 GUI 里发意图用）。
    ng::core::IIntentExecutor &executor() override;

    // 插槽登记表（MainWindow 用它声明骨架插槽）。
    ng::core::UiSlotRegistry &slotRegistry() { return m_slots; }

private:
    QMainWindow *m_window = nullptr;
    ng::core::UiSlotRegistry &m_slots;
    RibbonBar *m_ribbon = nullptr;
    DockManager *m_docks = nullptr;
    PlotCanvas *m_canvas = nullptr;
    ng::core::IIntentExecutor *m_executor = nullptr;
};

} // namespace ng::gui
