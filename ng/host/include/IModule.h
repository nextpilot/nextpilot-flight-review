#pragma once

#include "AppContext.h"
#include "DataRepository.h"
#include "Domain.h"
#include "IIntentExecutor.h"
#include "IPanelContribution.h"
#include "Intent.h"
#include "JobRecorder.h"
#include "ModuleInfo.h"
#include "UiSlotRegistry.h"

#include <QObject>

class QWidget;

namespace nextpilot::plotscope::host
{

class RuntimeHost;

// UI 上下文：headless 时宿主不构造它，模块 onAttach 就不会被调用。
//
// 它只提供"放置"能力，不接管 UI 对象的所有权——所有权始终在模块。
// QWidget 只做前置声明：host 层不链接 Qt Widgets，headless 构建保持轻量。
class UiContext
{
public:
    virtual ~UiContext() = default;

    // 把一个面板停靠到指定区域（dock.left / dock.right / dock.bottom / dock.top）。
    // titleOverride 非空时用它作停靠标题（领域面板用自己的描述覆盖通用插槽标签）。
    virtual void placePanel(QWidget *widget, const QString &area,
                            const QString &titleOverride = QString()) = 0;

    // 把一个控件放进中央画布区（替换当前内容）。
    virtual void placeCanvas(QWidget *widget) = 0;

    // 声明一个 UI 插槽（ribbon 页签/分组、停靠区、菜单）。
    // 模块用它声明自己的容器，再往里放动作——这样新增模块能自带界面入口，
    // 而 app 骨架与其他模块代码都不必修改。
    virtual void declareSlot(const QString &area, const QString &label,
                             nextpilot::plotscope::registry::SlotKind kind,
                             const QString &parentArea = QString()) = 0;

    // 往已声明的插槽里放一个动作（ribbon 按钮 / 菜单项）。
    // 归属插槽由 action.area 单一来源给出——不另传 area 参数，
    // 杜绝"参数 area 与 action.area 不一致"的双源隐患。
    virtual void addAction(const nextpilot::plotscope::registry::UiAction &action) = 0;

    // 以**可整体重置**的动态 provider 注册动作（分析 gallery 的"最近使用/收藏"组）。
    // 与 addAction（固定 provider "module"，终身有效）不同：动态条目随运行状态变化，
    // 需要成组刷新——先 resetDynamicActions 清掉该 provider 的旧条目，再全量重注册。
    virtual void addDynamicAction(const nextpilot::plotscope::registry::UiAction &action,
                                  const QString &provider) = 0;
    // 清空某动态 provider 的全部动作（外壳随后自动重建 ribbon）。
    virtual void resetDynamicActions(const QString &provider) = 0;

    // 请求 app 骨架提供一个部件，放到指定区域。
    //
    // 模块只描述"我需要什么部件"（用部件类型标识，如 "dataset-tree"），
    // 由 app 骨架决定用哪个 QWidget 实现并停靠。这样：
    //   - 模块不依赖 Qt Widgets，headless（CLI）构建保持纯净；
    //   - 新增模块自带界面 = 声明一个部件类型 + 动作，无需改 GUI 层。
    // 返回部件的宿主引用（QWidget 前置声明），模块可用它设置数据/标题等。
    virtual QWidget *requestPanel(const QString &area, const QString &panelType) = 0;

    // 请求一个**领域面板**（形态由知识包决定，模块不定义形态）。
    //
    // 什么时候需要它：有些面板的形态**本身就是领域知识**——
    // CAN 报文浏览器长什么样取决于 DBC 定义的信号，波形调参器取决于参数表。
    // 这类面板通用视图层不该认识（views/ 会长成领域杂物箱），
    // 器官也给不出（器官是机制，不懂业务）。
    // 于是形态归**知识包**：它用 IPanelContribution 声明
    // "我提供 car.can-browser，标题'CAN 报文'"，并在 GUI 侧注册实现。
    //
    // 模块怎么知道该请求哪个？它不写死 id，而是先问
    // availableDomainPanels(domain)——拿到该领域提供的面板清单，
    // 再按 id 请求。于是：
    //   - 模块只认**领域**（它的业务范围），不认具体面板；
    //   - 面板清单由知识包决定，装/卸知识包即时反映；
    //   - headless 下也能问（清单是纯数据）。
    //
    // domain 传 Domain::General 表示不限领域，返回全部可用领域面板。
    virtual QList<nextpilot::plotscope::registry::PanelSpec> availableDomainPanels(
        nextpilot::plotscope::registry::Domain domain) const = 0;

    // 按 id 请求领域面板（id 来自 availableDomainPanels）。
    // 与 requestPanel 的区别：这里不要求 views/ 预先注册，
    // 实现由**知识包的呈现层**注册进同一张工厂表。
    // 返回 nullptr 表示该面板当前不可用（如知识包未装载）。
    virtual QWidget *requestDomainPanel(const QString &area, const QString &panelId) = 0;

    // 宿主执行器：模块在 GUI 里点击动作后，用它发意图（与脚本/CLI 同一路径）。
    virtual nextpilot::plotscope::intent::IIntentExecutor &executor() = 0;

    // 选取一个文件路径（GUI 交互能力，由外壳实现）。
    //
    // 为什么放在 UiContext 而不是让模块自己开对话框：
    //   - 模块层不链接 Qt Widgets（headless 构建保持纯净），开不了 QFileDialog；
    //   - 文件对话框是**外壳**的交互能力，不是业务语义；
    //   - 模块只拿到"一个路径"，拿到之后照样走 data.load 意图——
    //     于是 GUI 与 CLI/脚本加载数据走的是同一条路径，行为一致。
    // 返回空串表示用户取消。
    virtual QString pickFilePath(const QString &title, const QString &filter) = 0;
};

// 模块拿到的运行时上下文：只有能力，没有全局单例、没有 MainWindow*。
class ModuleContext
{
public:
    ModuleContext(nextpilot::plotscope::context::AppContext &appCtx, RuntimeHost &host)
        : m_appCtx(appCtx), m_host(host) {}

    // 进程级运行态的**唯一入口**：数据集、设置、工具箱、数据源、摄取管道都在这里。
    //
    // 架构约定（重要）：器官只通过 ctx.app() 取 app 级状态，
    // 不再混用 ctx.data() 与 ctx.host().settings() 两条路径——
    // 那两条路径正是"状态分散持有"的症状。
    nextpilot::plotscope::context::AppContext &app() { return m_appCtx; }
    const nextpilot::plotscope::context::AppContext &app() const { return m_appCtx; }

    // 便捷转发（保留以兼容既有器官代码；新代码请用 app()）。
    nextpilot::plotscope::dataset::DataRepository &data() { return m_appCtx.data(); }
    RuntimeHost &host() { return m_host; }

    // 脚本/自动化能力：拿到意图执行器与可选录制器。
    // 返回的是宿主实现的执行器抽象，模块只依赖接口不依赖宿主内部。
    nextpilot::plotscope::intent::IIntentExecutor &executor();
    nextpilot::plotscope::intent::JobRecorder *recorder();

    // 模块注册自己能处理的意图 op。
    void registerIntentHandler(nextpilot::plotscope::intent::IIntentHandler *handler);

private:
    nextpilot::plotscope::context::AppContext &m_appCtx;
    RuntimeHost &m_host;
};

// 统一模块接口。器官与知识包都实现它，宿主一视同仁地装载。
class IModule : public QObject
{
    Q_OBJECT
public:
    explicit IModule(QObject *parent = nullptr) : QObject(parent) {}
    ~IModule() override = default;

    virtual nextpilot::plotscope::host::ModuleInfo info() const = 0;

    // 阶段一：声明能力、订阅事件、注册意图处理器。不碰 UI。
    virtual void initialize(ModuleContext &ctx) = 0;

    // 阶段二：创建 UI 并 parent 给自己，然后请求宿主放置。
    // headless 时宿主不调用；默认空实现。
    virtual void onAttach(UiContext &) {}

    // 阶段三：可选，模块被卸载前的清理。QObject 析构负责资源回收。
    virtual void onDetach() {}
};

} // namespace nextpilot::plotscope::host
