#pragma once

// 工作台贡献契约（居 libs/workbench）：IWorkbenchContribution / IUiContext。
// 纯接口面——运行时（装载、生命周期）在 host/RuntimeHost。
// A1b（IKXUTD）：由旧模块接口改名而来，initialize 只收服务容器
// InstantiationService&；模块不再拿 RuntimeHost / AppContext，按 VSCode 名字取服务。
// ModuleInfo 仍居原模块契约库（物理目录改名归 A3b，本项只改 include 路径与类名）。

#include "InstantiationService.h"
#include "DatasetHub.h"
#include "Domain.h"
#include "IIntentExecutor.h"
#include "Contribution.h"
#include "Intent.h"
#include "JobRecorder.h"
#include "ModuleInfo.h"
#include "UiSlotHub.h"
#include "ModuleContributions.h"

#include <QObject>

class QWidget;

namespace nextpilot::plotscope::host {

// UI 上下文：headless 时宿主不构造它，模块 onAttach 就不会被调用。
//
// 它只提供"放置"能力，不接管 UI 对象的所有权——所有权始终在模块。
// QWidget 只做前置声明：host 层不链接 Qt Widgets，headless 构建保持轻量。
class IUiContext
{
public:
    virtual ~IUiContext() = default;

    // A8b-M2 裁定 52442006 Step 3：灌回已搬出壳——原 installContributions 虚函数
    // 删除，改由装配层 ContributionsInstaller（IContributionsInstaller）实现，
    // host.activate 收集 contributes 后直接调它。IUiContext 只剩工作台服务。

    // A8b-M2 裁定 52441074：声明类方法（declare*/addAction/bindActionProvider/
    // refreshActions）已从 IUiContext 移除——模块改走 contributes() 声明式数据，
    // 宿主收集后经 IContributionsInstaller::install 一次性灌回。

    // 推送状态文本（不轮询）：模块在数据变化时调用，壳更新对应项的显示。
    // 未声明的 id 被忽略（headless 下无实现，声明/推送都不影响 CLI 纯净）。
    virtual void updateStatus(const QString &id, const QString &text) = 0;

    // 瞬时消息（对应 QStatusBar::showMessage 的短时提示），壳统一呈现。
    virtual void flashStatus(const QString &text) = 0;

    // 更新 Activity Bar 徽标数量（0 表示不显示）。
    virtual void setActivityBadge(const QString &activityId, int count) = 0;

    // 请求 app 骨架提供一个部件，放到指定逻辑区域——通用面板与扩展包领域面板的唯一入口。
    //
    // 模块只描述"我需要什么部件"（用部件类型标识，如 "dataset-tree" / "car.can-browser"），
    // 由 app 骨架决定用哪个 QWidget 实现并停靠。实现先查模块/视图工厂表，
    // 未命中再按扩展包 PanelSpec 清单取标题/图标元数据；工厂未注册返回 nullptr。这样：
    //   - 模块不依赖 Qt Widgets，headless（CLI）构建保持纯净；
    //   - 新增模块自带界面 = 声明一个部件类型 + 动作，无需改 GUI 层。
    // 返回部件的宿主引用（QWidget 前置声明），模块可用它设置数据/标题等。
    virtual QWidget *requestPanel(nextpilot::plotscope::registry::Region region, const QString &panelType) = 0;

    // ---- A2b 笔2b：视图/编辑器打开入口（取代 requestPanel，requestPanel 待旧面清除步删除）----
    // 打开一个已声明视图（落区由 viewsContainers[].area 决定，模块不指定 Region）；
    // 已创建则置前复用。未声明/未注册返回 nullptr。壳内部补齐 AppContext/executor/parent。
    virtual QWidget *openView(const QString &viewId) = 0;
    // 在中央区打开编辑器：viewType 直接指定编辑器类型，ref 由 owner 模块构造、壳不解析 uri。
    // 未注册 provider 或 canOpen 拒绝返回 nullptr。
    virtual QWidget *openEditor(const QString &viewType, const nextpilot::plotscope::registry::ResourceRef &ref) = 0;

    // 查询当前可用的领域面板清单（纯数据，headless 也能问）。
    //
    // 模块怎么知道该请求哪个？它不写死 id，而是先问
    // availableDomainPanels(domain)——拿到该领域提供的面板清单，
    // 再按 id 走唯一入口 requestPanel 请求。于是：
    //   - 模块只认**领域**（它的业务范围），不认具体面板；
    //   - 面板清单由扩展包决定，装/卸扩展包即时反映；
    //   - headless 下也能问（清单是纯数据）。
    //
    // domain 传 Domain::General 表示不限领域，返回全部可用领域面板。
    virtual QList<nextpilot::plotscope::registry::PanelSpec>
    availableDomainPanels(nextpilot::plotscope::registry::Domain domain) const = 0;

    // 宿主执行器：模块在 GUI 里点击动作后，用它发意图（与脚本/CLI 同一路径）。
    virtual nextpilot::plotscope::intent::IIntentExecutor &executor() = 0;

    // 选取一个文件路径（GUI 交互能力，由外壳实现）。
    //
    // 为什么放在 IUiContext 而不是让模块自己开对话框：
    //   - 模块层不链接 Qt Widgets（headless 构建保持纯净），开不了 QFileDialog；
    //   - 文件对话框是**外壳**的交互能力，不是业务语义；
    //   - 模块只拿到"一个路径"，拿到之后照样走 data.load 意图——
    //     于是 GUI 与 CLI/脚本加载数据走的是同一条路径，行为一致。
    // 返回空串表示用户取消。
    virtual QString pickFilePath(const QString &title, const QString &filter) = 0;
};

// 统一工作台贡献接口。基础功能模块实现它，宿主一视同仁地装载。
class IWorkbenchContribution : public QObject
{
    // 纯抽象接口：不声明 Q_OBJECT，moc 由各具体模块子类负责（同 IUiContext）。
public:
    explicit IWorkbenchContribution(QObject *parent = nullptr) : QObject(parent) {}
    ~IWorkbenchContribution() override = default;

    virtual nextpilot::plotscope::host::ModuleInfo info() const = 0;

    // 阶段一：声明能力、订阅事件、注册命令。不碰 UI。
    // 只收服务容器：模块按 VSCode 名字从 services 取能力，不接触宿主/AppContext。
    virtual void initialize(nextpilot::plotscope::instantiation::InstantiationService &services) = 0;

    // A8b-M2 裁定 52441074：UI 声明改为声明式数据（单一通道）。
    // 宿主在 initialize 后收集所有模块的 contributes()，统一灌回壳的 slot/status/theme；
    // 模块不再过程式调 IUiContext::declare*/addAction。默认空，兼容不贡献 UI 的模块。
    virtual nextpilot::plotscope::registry::ModuleContributions contributes() const { return {}; }

    // 阶段二：创建 UI 并 parent 给自己，然后请求宿主放置。
    // headless 时宿主不调用；默认空实现。
    virtual void onAttach(IUiContext &) {}

    // 阶段三：可选，模块被卸载前的清理。QObject 析构负责资源回收。
    virtual void onDetach() {}
};

} // namespace nextpilot::plotscope::host
