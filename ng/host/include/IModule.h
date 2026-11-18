#pragma once

#include "AppContext.h"
#include "DataRepository.h"
#include "IIntentExecutor.h"
#include "Intent.h"
#include "JobRecorder.h"
#include "ModuleInfo.h"
#include "UiSlotRegistry.h"

#include <QObject>

class QWidget;

namespace ng::host
{

class ModuleHost;

// UI 上下文：headless 时宿主不构造它，模块 onAttach 就不会被调用。
//
// 它只提供"放置"能力，不接管 UI 对象的所有权——所有权始终在模块。
// QWidget 只做前置声明：host 层不链接 Qt Widgets，headless 构建保持轻量。
class UiContext
{
public:
    virtual ~UiContext() = default;

    // 把一个面板停靠到指定区域（dock.left / dock.right / dock.bottom / dock.top）。
    virtual void placePanel(QWidget *widget, const QString &area) = 0;

    // 把一个控件放进中央画布区（替换当前内容）。
    virtual void placeCanvas(QWidget *widget) = 0;

    // 声明一个 UI 插槽（ribbon 页签/分组、停靠区、菜单）。
    // 模块用它声明自己的容器，再往里放动作——这样新增模块能自带界面入口，
    // 而 app 骨架与其他模块代码都不必修改。
    virtual void declareSlot(const QString &area, const QString &label,
                             ng::core::SlotKind kind,
                             const QString &parentArea = QString()) = 0;

    // 往已声明的插槽里放一个动作（ribbon 按钮 / 菜单项）。
    virtual void addAction(const QString &area, const ng::core::UiAction &action) = 0;

    // 请求 app 骨架提供一个部件，放到指定区域。
    //
    // 模块只描述"我需要什么部件"（用部件类型标识，如 "dataset-tree"），
    // 由 app 骨架决定用哪个 QWidget 实现并停靠。这样：
    //   - 模块不依赖 Qt Widgets，headless（CLI）构建保持纯净；
    //   - 新增模块自带界面 = 声明一个部件类型 + 动作，无需改 GUI 层。
    // 返回部件的宿主引用（QWidget 前置声明），模块可用它设置数据/标题等。
    virtual QWidget *requestPanel(const QString &area, const QString &panelType) = 0;

    // 宿主执行器：模块在 GUI 里点击动作后，用它发意图（与脚本/CLI 同一路径）。
    virtual ng::core::IIntentExecutor &executor() = 0;
};

// 模块拿到的运行时上下文：只有能力，没有全局单例、没有 MainWindow*。
class ModuleContext
{
public:
    ModuleContext(ng::core::AppContext &appCtx, ModuleHost &host)
        : m_appCtx(appCtx), m_host(host) {}

    // 进程级运行态的**唯一入口**：数据集、设置、工具箱、数据源、摄取管道都在这里。
    //
    // 架构约定（重要）：器官只通过 ctx.app() 取 app 级状态，
    // 不再混用 ctx.data() 与 ctx.host().settings() 两条路径——
    // 那两条路径正是"状态分散持有"的症状。
    ng::core::AppContext &app() { return m_appCtx; }
    const ng::core::AppContext &app() const { return m_appCtx; }

    // 便捷转发（保留以兼容既有器官代码；新代码请用 app()）。
    ng::core::DataRepository &data() { return m_appCtx.data(); }
    ModuleHost &host() { return m_host; }

    // 脚本/自动化能力：拿到意图执行器与可选录制器。
    // 返回的是宿主实现的执行器抽象，模块只依赖接口不依赖宿主内部。
    ng::core::IIntentExecutor &executor();
    ng::core::JobRecorder *recorder();

    // 模块注册自己能处理的意图 op。
    void registerIntentHandler(ng::core::IIntentHandler *handler);

private:
    ng::core::AppContext &m_appCtx;
    ModuleHost &m_host;
};

// 统一模块接口。器官与知识包都实现它，宿主一视同仁地装载。
class IModule : public QObject
{
    Q_OBJECT
public:
    explicit IModule(QObject *parent = nullptr) : QObject(parent) {}
    ~IModule() override = default;

    virtual ng::host::ModuleInfo info() const = 0;

    // 阶段一：声明能力、订阅事件、注册意图处理器。不碰 UI。
    virtual void initialize(ModuleContext &ctx) = 0;

    // 阶段二：创建 UI 并 parent 给自己，然后请求宿主放置。
    // headless 时宿主不调用；默认空实现。
    virtual void onAttach(UiContext &) {}

    // 阶段三：可选，模块被卸载前的清理。QObject 析构负责资源回收。
    virtual void onDetach() {}
};

} // namespace ng::host
