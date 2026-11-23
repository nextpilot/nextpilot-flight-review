#pragma once

#include "AppContext.h"
#include "IModule.h"
#include "UiSlotHub.h"

#include <QHash>
#include <QObject>
#include <QPointer>

class QMainWindow;

namespace nextpilot::plotscope::gui
{

class ShellBinder;

// GUI 上下文：宿主传给模块的唯一 UI 接口实现。
//
// 职责单一：把"模块/工具箱想放的东西"路由给 ShellBinder
// （ribbon 动作、停靠面板、中央画布）。它不认识任何具体业务，
// 也不决定"哪个动作做什么"——动作语义由模块声明（意图 op），执行由宿主分派。
//
// 它**不认识任何功能视图**（views/ 下的类）：视图通过 PanelFactoryHub
// 自注册自己，模块只声明类型字符串。
class GuiContext final : public QObject, public nextpilot::plotscope::host::IUiContext
{
    Q_OBJECT
public:
    GuiContext(QMainWindow *window, nextpilot::plotscope::registry::UiSlotHub &slotReg, ShellBinder *binder,
               nextpilot::plotscope::context::AppContext &appCtx, nextpilot::plotscope::intent::IIntentExecutor *executor = nullptr,
               QObject *parent = nullptr);

    // nextpilot::plotscope::host::IUiContext
    void placePanel(QWidget *widget, const QString &area,
                    const QString &titleOverride = QString()) override;
    // 带稳定 pageKey 的放置（面板类型 id）：视图页逐页开关按钮按它定位页签。
    void placePanelWithKey(QWidget *widget, const QString &area, const QString &title,
                           const QString &pageKey);
    void placeCanvas(QWidget *widget, const QString &titleOverride = QString()) override;
    void declareSlot(const QString &area, const QString &label, nextpilot::plotscope::registry::SlotKind kind,
                     const QString &parentArea = QString()) override;
    void declareActivity(const nextpilot::plotscope::registry::ActivitySpec &activity) override;
    void setActivityBadge(const QString &activityId, int count) override;
    void addAction(const nextpilot::plotscope::registry::UiAction &action) override;
    void addDynamicAction(const nextpilot::plotscope::registry::UiAction &action,
                          const QString &provider) override;
    void resetDynamicActions(const QString &provider) override;

    // 请求 app 外壳提供一个部件，放到指定区域。
    QWidget *requestPanel(const QString &area, const QString &panelType) override;

    // 领域面板（形态由扩展包决定）：清单 + 按 id 请求。
    QList<nextpilot::plotscope::registry::PanelSpec> availableDomainPanels(
        nextpilot::plotscope::registry::Domain domain) const override;
    QWidget *requestDomainPanel(const QString &area, const QString &panelId) override;

    // 宿主执行器（模块在 GUI 里发意图用）。
    nextpilot::plotscope::intent::IIntentExecutor &executor() override;

    // 文件选取（GUI 交互能力）。
    QString pickFilePath(const QString &title, const QString &filter) override;

    // 插槽登记表（MainWindow 用它声明骨架插槽）。
    nextpilot::plotscope::registry::UiSlotHub &slotHub() { return m_slots; }

    // 应用上下文（视图用它取数据与共享状态）。
    nextpilot::plotscope::context::AppContext &app() { return m_appCtx; }

private:
    QMainWindow *m_window = nullptr;
    nextpilot::plotscope::registry::UiSlotHub &m_slots;
    ShellBinder *m_binder = nullptr;
    nextpilot::plotscope::context::AppContext &m_appCtx;
    nextpilot::plotscope::intent::IIntentExecutor *m_executor = nullptr;

    // 面板去重（按 area/panelType）：重复 requestPanel 同一类型返回既有实例，
    // 不再新建页签（脚本编辑器/数据表格的"再打开"都是复用语义）。
    // QPointer 兜底：页签被用户关掉后自动清空，下次请求重新创建。
    QHash<QString, QPointer<QWidget>> m_panels;

    // 面板图标（widget -> 图标名）：requestPanel/requestDomainPanel 从
    // PanelFactoryHub / PanelSpec 取到图标后先记这里，placePanel/placeCanvas
    // 放下时消费掉（take），所以不会长期持有裸指针。
    QHash<const QWidget *, QString> m_panelIcon;
};

} // namespace nextpilot::plotscope::gui
