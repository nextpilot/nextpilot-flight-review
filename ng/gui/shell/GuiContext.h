#pragma once

#include "IModule.h"
#include "UiSlotRegistry.h"

#include <QObject>

class QMainWindow;

namespace ng::core
{
class AppContext;
}

namespace ng::gui
{

class ShellBinder;

// GUI 上下文：宿主传给器官的唯一 UI 接口实现。
//
// 职责单一：把"器官/工具箱想放的东西"路由给 ShellBinder
// （ribbon 动作、停靠面板、中央画布）。它不认识任何具体业务，
// 也不决定"哪个动作做什么"——动作语义由器官声明（意图 op），执行由宿主分派。
//
// 它**不认识任何功能视图**（views/ 下的类）：视图通过 PanelFactoryRegistry
// 自注册自己，器官只声明类型字符串。
class GuiContext final : public QObject, public ng::host::UiContext
{
    Q_OBJECT
public:
    GuiContext(QMainWindow *window, ng::core::UiSlotRegistry &slotReg, ShellBinder *binder,
               ng::core::AppContext &appCtx, ng::core::IIntentExecutor *executor = nullptr,
               QObject *parent = nullptr);

    // ng::host::UiContext
    void placePanel(QWidget *widget, const QString &area) override;
    void placeCanvas(QWidget *widget) override;
    void declareSlot(const QString &area, const QString &label, ng::core::SlotKind kind,
                     const QString &parentArea = QString()) override;
    void addAction(const QString &area, const ng::core::UiAction &action) override;

    // 请求 app 外壳提供一个部件，放到指定区域。
    QWidget *requestPanel(const QString &area, const QString &panelType) override;

    // 宿主执行器（模块在 GUI 里发意图用）。
    ng::core::IIntentExecutor &executor() override;

    // 插槽登记表（MainWindow 用它声明骨架插槽）。
    ng::core::UiSlotRegistry &slotRegistry() { return m_slots; }

    // 应用上下文（视图用它取数据与共享状态）。
    ng::core::AppContext &app() { return m_appCtx; }

private:
    QMainWindow *m_window = nullptr;
    ng::core::UiSlotRegistry &m_slots;
    ShellBinder *m_binder = nullptr;
    ng::core::AppContext &m_appCtx;
    ng::core::IIntentExecutor *m_executor = nullptr;
};

} // namespace ng::gui
