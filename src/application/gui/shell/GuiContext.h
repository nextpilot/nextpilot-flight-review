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
    void declareRibbonPage(const QString &id, const QString &label, int order) override;
    void declareRibbonGroup(const QString &id, const QString &label,
                            const QString &pageId, int order = 0) override;
    void declareActivity(const nextpilot::plotscope::registry::ActivitySpec &activity) override;
    void setActivityBadge(const QString &activityId, int count) override;
    void addAction(const nextpilot::plotscope::registry::UiAction &action) override;
    void bindActionProvider(
        const QString &area, const QString &provider,
        std::function<QList<nextpilot::plotscope::registry::UiAction>()> providerFn) override;
    void refreshActions() override;

    // 请求 app 外壳提供一个部件，放到指定逻辑区域（通用面板与扩展包领域面板同一入口）。
    QWidget *requestPanel(nextpilot::plotscope::registry::Region region,
                          const QString &panelType) override;

    // 领域面板清单查询（纯数据）：请求仍走 requestPanel。
    QList<nextpilot::plotscope::registry::PanelSpec> availableDomainPanels(
        nextpilot::plotscope::registry::Domain domain) const override;

    // 宿主执行器（模块在 GUI 里发意图用）。
    nextpilot::plotscope::intent::IIntentExecutor &executor() override;

    // 文件选取（GUI 交互能力）。
    QString pickFilePath(const QString &title, const QString &filter) override;

    // 插槽登记表（MainWindow 用它声明骨架插槽）。
    nextpilot::plotscope::registry::UiSlotHub &slotHub() { return m_slots; }

    // 应用上下文（视图用它取数据与共享状态）。
    nextpilot::plotscope::context::AppContext &app() { return m_appCtx; }

private:
    // 壳内部装配路径（不进 IUiContext 接口面）：模块只能经 requestPanel 走工厂表，
    // 不允许自己造 QWidget 直接递进来。
    // dockWidget：停靠到指定区域；pageKey = 面板类型 id（视图页开关按钮按它定位页签）。
    void dockWidget(QWidget *widget, nextpilot::plotscope::registry::Region region,
                    const QString &title, const QString &pageKey);
    // dockCentral：放进中央画布区（首到者占位，后来者并成页签）。
    void dockCentral(QWidget *widget, const QString &titleOverride = QString());

    QMainWindow *m_window = nullptr;
    nextpilot::plotscope::registry::UiSlotHub &m_slots;
    ShellBinder *m_binder = nullptr;
    nextpilot::plotscope::context::AppContext &m_appCtx;
    nextpilot::plotscope::intent::IIntentExecutor *m_executor = nullptr;

    // 面板去重（按 area/panelType）：重复 requestPanel 同一类型返回既有实例，
    // 不再新建页签（脚本编辑器/数据表格的"再打开"都是复用语义）。
    // QPointer 兜底：页签被用户关掉后自动清空，下次请求重新创建。
    QHash<QString, QPointer<QWidget>> m_panels;

    // 面板图标（widget -> 图标名）：requestPanel 从
    // PanelFactoryHub / PanelSpec 取到图标后先记这里，dockWidget/dockCentral
    // 放下时消费掉（take），所以不会长期持有裸指针。
    QHash<const QWidget *, QString> m_panelIcon;
};

} // namespace nextpilot::plotscope::gui
