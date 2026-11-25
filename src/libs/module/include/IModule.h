#pragma once

// 模块契约（居 libs/module）：IModule / ModuleInfo / IUiContext。
// 纯接口面——运行时（装载、生命周期）在 host/RuntimeHost，
// 与 IIntentExecutor 居 libs/intent 同一先例：契约在 libs，进程层只做实例化与运行。
//
// 模块初始化只拿 RuntimeHost&：它既是执行器（IIntentExecutor），又持有 AppContext
// （app()/data()/settings()/recorder()/registerIntentHandler() 全在它上面）。
// 不再有一张"档案室钥匙 + 管家热线"的介绍信——递给模块的就是管家本人。

#include "AppContext.h"
#include "DatasetHub.h"
#include "Domain.h"
#include "IIntentExecutor.h"
#include "Contribution.h"
#include "Intent.h"
#include "JobRecorder.h"
#include "ModuleInfo.h"
#include "UiSlotHub.h"

#include <QObject>

class QWidget;

namespace nextpilot::plotscope::host
{

class RuntimeHost;

// UI 上下文：headless 时宿主不构造它，模块 onAttach 就不会被调用。
//
// 它只提供"放置"能力，不接管 UI 对象的所有权——所有权始终在模块。
// QWidget 只做前置声明：host 层不链接 Qt Widgets，headless 构建保持轻量。
class IUiContext
{
public:
    virtual ~IUiContext() = default;

    // 声明 Ribbon 页签（纯数据容器）：模块把自己的业务页登记进壳的 Ribbon，
    // 壳不再预置业务页（D13）。同 id 重复声明先到者为准。
    virtual void declareRibbonPage(const QString &id, const QString &label, int order) = 0;

    // 声明 Ribbon 分组，挂到指定页签下；order 决定同页内左右位置。
    virtual void declareRibbonGroup(const QString &id, const QString &label,
                                    const QString &pageId, int order = 0) = 0;

    // 声明 Activity Bar 入口。模块只描述 id/标签/图标/对应 ribbon 页/左栏 section，
    // 控件由外壳创建；headless 下不调用 onAttach，声明仍可落表。
    virtual void declareActivity(const nextpilot::plotscope::registry::ActivitySpec &activity) = 0;

    // 声明模块贡献的语义色 token（验收 8 / VS Code 对位）：模块说自己需要哪些语义色
    // 及兜底色，壳汇总后灌进主题；内置主题未定义的键才用兜底色。headless 不调 onAttach，
    // 故模块 token 只在 GUI 装配路径出现，不影响 CLI 纯净。
    virtual void declareThemeToken(const nextpilot::plotscope::registry::ThemeTokenSpec &token) = 0;

    // 声明状态栏项（验收 7 / D8 推模式）：模块只描述 id/side/priority/when 等元数据，
    // 控件由壳按 side/priority 排布；同 id 重复声明先到者为准。
    virtual void declareStatus(const nextpilot::plotscope::registry::StatusSpec &status) = 0;

    // 推送状态文本（不轮询）：模块在数据变化时调用，壳更新对应项的显示。
    // 未声明的 id 被忽略（headless 下无实现，声明/推送都不影响 CLI 纯净）。
    virtual void updateStatus(const QString &id, const QString &text) = 0;

    // 瞬时消息（对应 QStatusBar::showMessage 的短时提示），壳统一呈现。
    virtual void flashStatus(const QString &text) = 0;

    // 更新 Activity Bar 徽标数量（0 表示不显示）。
    virtual void setActivityBadge(const QString &activityId, int count) = 0;

    // 往已声明的插槽里放一个动作（ribbon 按钮 / 图库项）。
    // 归属插槽由 action.slotId 单一来源给出——不另传 slotId 参数，
    // 杜绝"参数 area 与 action.slotId 不一致"的双源隐患。
    // 动态可见性用 action.when 表达（命令层 when 求值器），不再设动态 provider 通道。
    virtual void addAction(const nextpilot::plotscope::registry::UiAction &action) = 0;

    // 注册动作提供器：ribbon 装配时实时回调取当前条目（MRU / 收藏等运行期变化的清单）。
    // 提供器只返回纯数据 UiAction；条目内容变化后调 refreshActions() 触发重建。
    virtual void bindActionProvider(
        const QString &area, const QString &provider,
        std::function<QList<nextpilot::plotscope::registry::UiAction>()> providerFn) = 0;

    // 提供器内容已变化：通知外壳重建 ribbon（不直接碰控件）。
    virtual void refreshActions() = 0;

    // 请求 app 骨架提供一个部件，放到指定逻辑区域——通用面板与扩展包领域面板的唯一入口。
    //
    // 模块只描述"我需要什么部件"（用部件类型标识，如 "dataset-tree" / "car.can-browser"），
    // 由 app 骨架决定用哪个 QWidget 实现并停靠。实现先查模块/视图工厂表，
    // 未命中再按扩展包 PanelSpec 清单取标题/图标元数据；工厂未注册返回 nullptr。这样：
    //   - 模块不依赖 Qt Widgets，headless（CLI）构建保持纯净；
    //   - 新增模块自带界面 = 声明一个部件类型 + 动作，无需改 GUI 层。
    // 返回部件的宿主引用（QWidget 前置声明），模块可用它设置数据/标题等。
    virtual QWidget *requestPanel(nextpilot::plotscope::registry::Region region,
                                  const QString &panelType) = 0;

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
    virtual QList<nextpilot::plotscope::registry::PanelSpec> availableDomainPanels(
        nextpilot::plotscope::registry::Domain domain) const = 0;

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

// 统一模块接口。模块与扩展包都实现它，宿主一视同仁地装载。
class IModule : public QObject
{
    Q_OBJECT
public:
    explicit IModule(QObject *parent = nullptr) : QObject(parent) {}
    ~IModule() override = default;

    virtual nextpilot::plotscope::host::ModuleInfo info() const = 0;

    // 阶段一：声明能力、订阅事件、注册意图处理器。不碰 UI。
    // host 既是执行器，又持有 AppContext（app()/data()/settings()/recorder()/...）。
    virtual void initialize(RuntimeHost &host) = 0;

    // 阶段二：创建 UI 并 parent 给自己，然后请求宿主放置。
    // headless 时宿主不调用；默认空实现。
    virtual void onAttach(IUiContext &) {}

    // 阶段三：可选，模块被卸载前的清理。QObject 析构负责资源回收。
    virtual void onDetach() {}
};

} // namespace nextpilot::plotscope::host
