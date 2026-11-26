#pragma once

// A8b-M2 裁定 52442006 Step 5：视图声明的读回与打开经本服务——壳（sideBar 容器）
// 不直接持有 ViewRegistry，声明/打开链路都走接口；实现放 assembly
// （ViewsServiceAdapter，内部转 ViewRegistry）。VSCode 对位
// `vscode.window.registerWebviewViewProvider` 的消费面 + `viewsContainers` 查询。

#include "ContributionParser.h" // ContributedView / ContributedViewContainer（纯数据）

class QWidget;

namespace nextpilot::plotscope::context
{
class AppContext;
}
namespace nextpilot::plotscope::intent
{
class IIntentExecutor;
}

namespace nextpilot::plotscope::workbench
{

class IViewsService
{
public:
    virtual ~IViewsService() = default;

    // 某区域（"sidebar"/"bottom"/"auxiliary"）当前声明的容器（按 order 升序）。
    virtual QList<nextpilot::plotscope::registry::ContributedViewContainer>
    containersIn(const QString &area) const = 0;

    // 某容器当前声明的视图（按 order 升序）。
    virtual QList<nextpilot::plotscope::registry::ContributedView>
    viewsInContainer(const QString &containerId) const = 0;

    // 视图声明（未命中返回 owner 为空的默认值；用 owner 判空）。
    virtual nextpilot::plotscope::registry::ContributedView
    viewForId(const QString &viewId) const = 0;

    // 打开视图内容（工厂经 ViewRegistry::registerViewProvider 注册；未注册返回 nullptr）。
    // 已创建则置前（show/raise）并返回原控件，未创建则经工厂创建（A2b 笔 2a）。
    virtual QWidget *openView(const QString &viewId,
                              nextpilot::plotscope::context::AppContext &appCtx,
                              nextpilot::plotscope::intent::IIntentExecutor &executor,
                              QWidget *parent) = 0;
};

// 纯接口（无 QObject 基类）：registerSingleton 存接口地址，getService static_cast 恒安全。
} // namespace nextpilot::plotscope::workbench
