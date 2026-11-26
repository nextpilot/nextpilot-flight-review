#pragma once

#include "AppContext.h"
#include "IEditorService.h"
#include "IPanelsService.h"
#include "ISlotsService.h"
#include "IViewsService.h"
#include "IWorkbenchContribution.h"
#include "UiSlotTypes.h"

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
    GuiContext(QMainWindow *window, nextpilot::plotscope::workbench::ISlotsService *slotSvc, ShellBinder *binder,
               nextpilot::plotscope::context::AppContext &appCtx,
               nextpilot::plotscope::intent::IIntentExecutor *executor = nullptr,
               const nextpilot::plotscope::workbench::IPanelsService *panelsService = nullptr,
               nextpilot::plotscope::workbench::IViewsService *viewsService = nullptr,
               nextpilot::plotscope::workbench::IEditorService *editorService = nullptr, QObject *parent = nullptr);

    // nextpilot::plotscope::host::IUiContext（只留工作台服务；灌回已搬装配层，
    // 裁定 52442006 Step 3——IContributionsInstaller）
    void updateStatus(const QString &id, const QString &text) override;
    void flashStatus(const QString &text) override;
    void setActivityBadge(const QString &activityId, int count) override;

    // 请求 app 外壳提供一个部件，放到指定逻辑区域（通用面板与扩展包领域面板同一入口）。
    QWidget *requestPanel(nextpilot::plotscope::registry::Region region,
                          const QString &panelType) override;

    // A2b 笔2b：视图/编辑器打开入口——壳只转调 workbench 服务，不自己建控件、
    // 不自己判落区（落区由 viewsContainers[].area 决定，随各 owner 步迁移）。
    // openView：转调 IViewsService::openView（已开则置前复用，未注册出诊断返 nullptr）；
    // openEditor：转调 IEditorService::openEditor（ref 由 owner 构造、壳不解析）。
    // 未装配（bare 壳）服务为 nullptr 时诚实返回 nullptr。
    QWidget *openView(const QString &viewId) override;
    QWidget *openEditor(const QString &viewType, const nextpilot::plotscope::registry::ResourceRef &ref) override;

    // 领域面板清单查询（纯数据）：请求仍走 requestPanel。
    QList<nextpilot::plotscope::registry::PanelSpec> availableDomainPanels(
        nextpilot::plotscope::registry::Domain domain) const override;

    // 宿主执行器（模块在 GUI 里发意图用）。
    nextpilot::plotscope::intent::IIntentExecutor &executor() override;

    // 文件选取（GUI 交互能力）。
    QString pickFilePath(const QString &title, const QString &filter) override;

    // A8b-M2：装配层在 host.activate 后把各模块的声明式 contributes 灌进来。
    // 模块在 onAttach 声明的语义色 token（验收 8）：装配层灌回写进槽位表
    // （ISlotsService）；MainWindow 装配后与包 token 合并灌进 ThemeManager。
    // 按 id 去重，先声明者优先。
    QList<nextpilot::plotscope::registry::ThemeTokenSpec> moduleThemeTokens() const
    {
        return m_slots ? m_slots->moduleThemeTokens()
                       : QList<nextpilot::plotscope::registry::ThemeTokenSpec>{};
    }

    // 槽位表服务（裁定 52442006 Step 4）：MainWindow/test 经它读写插槽，
    // 不再出现扩展槽位表类型。未装配（bare 壳）时为 nullptr。
    nextpilot::plotscope::workbench::ISlotsService *slotsService() const { return m_slots; }

    // 模块声明的状态栏项与当前文本（验收 7）：MainWindow 装配后据此排布。
    QList<nextpilot::plotscope::registry::StatusSpec> statusSpecs() const
    {
        return m_appCtx.status().specs();
    }
    QString statusText(const QString &id) const { return m_appCtx.status().text(id); }

    // 应用上下文（视图用它取数据与共享状态）。
    nextpilot::plotscope::context::AppContext &app() { return m_appCtx; }

signals:
    // 状态栏项文本变化（D8 推模式）：壳订阅后刷新对应 QLabel。
    void statusTextChanged(const QString &id, const QString &text);
    // 新的状态栏项声明（装配期/运行期均可）：壳按 spec 创建控件。
    void statusSpecDeclared(const nextpilot::plotscope::registry::StatusSpec &spec);
    // 瞬时消息（原 QStatusBar::showMessage 的契约化入口）。
    void statusFlashed(const QString &text);

private:
    // 壳内部装配路径（不进 IUiContext 接口面）：模块只能经 requestPanel 走工厂表，
    // 不允许自己造 QWidget 直接递进来。
    // dockWidget：停靠到指定区域；pageKey = 面板类型 id（视图页开关按钮按它定位页签）。
    void dockWidget(QWidget *widget, nextpilot::plotscope::registry::Region region,
                    const QString &title, const QString &pageKey);
    // dockCentral：放进中央画布区（首到者占位，后来者并成页签）。
    void dockCentral(QWidget *widget, const QString &titleOverride = QString());

    QMainWindow *m_window = nullptr;
    nextpilot::plotscope::workbench::ISlotsService *m_slots = nullptr;
    ShellBinder *m_binder = nullptr;
    nextpilot::plotscope::context::AppContext &m_appCtx;
    nextpilot::plotscope::intent::IIntentExecutor *m_executor = nullptr;

    // 包声明读回（面板 spec / 领域面板清单）：裁定 52441633 §三，经 workbench
    // 服务接口注入，GuiContext 不直连扩展注册表。借用指针，生命周期归装配层。
    const nextpilot::plotscope::workbench::IPanelsService *m_panelsService = nullptr;

    // 视图/编辑器打开服务（A2b 笔2b 复审必修 A）：openView/openEditor 的唯一执行体，
    // 与 IPanelsService 同一路构造注入；借用指针，生命周期归装配层。未装配（bare 壳）为
    // nullptr，openView/openEditor 诚实返回 nullptr——不允许桥回旧工厂表。
    nextpilot::plotscope::workbench::IViewsService *m_viewsService = nullptr;
    nextpilot::plotscope::workbench::IEditorService *m_editorService = nullptr;

    // 状态栏：登记与文案都在 AppContext::status()（libs/context），
    // 本类只做"IUiContext → hub"的转发与信号中继；**不含节流**——
    // 某个键要不要限频是生产方（模块）的领域知识（D2），壳不认识业务键。

    // 面板去重（按 area/panelType）：重复 requestPanel 同一类型返回既有实例，
    // 不再新建页签（脚本编辑器/数据表格的"再打开"都是复用语义）。
    // QPointer 兜底：页签被用户关掉后自动清空，下次请求重新创建。
    QHash<QString, QPointer<QWidget>> m_panels;

    // openView 已开簿记（A2b 笔2b 复审必修 C）：viewId → 已 dock 的活实例。
    // 命中且存活 → 置前返回（不重复 dock）；页签被关后 QPointer 自动失效，
    // 下次 openView 重新走创建+摆放路径。
    QHash<QString, QPointer<QWidget>> m_openViews;

    // 面板图标（widget -> 图标名）：requestPanel 从
    // PanelFactoryHub / PanelSpec 取到图标后先记这里，dockWidget/dockCentral
    // 放下时消费掉（take），所以不会长期持有裸指针。
    QHash<const QWidget *, QString> m_panelIcon;
};

} // namespace nextpilot::plotscope::gui
