#pragma once

// automotive 领域视图装配（A2c）：car.can-browser → view automotive.canBrowser
// （owner "automotive"，area auxiliary）。
//
// ⚠️ 无 VSCode 对位：CAN 报文浏览器是领域面板，VSCode 无对应视图形态——差异登记
// docs/develop/vscode-alignment.md（A2c 表 A 末行）。
//
// 职责分界（A2b §7 同款）：views 声明与 provider 登记**都在本 TU**（声明先于注册；
// 领域包没有模块 initialize，声明物理在呈现层装配 TU，owner 仍写 "automotive"——
// 领域知识归领域包，壳不代声明）。工厂实现留 CanBrowserPanel.cpp。

namespace nextpilot::plotscope::context {
class AppContext;
}
namespace nextpilot::plotscope::intent {
class IIntentExecutor;
}
namespace nextpilot::plotscope::instantiation {
class InstantiationService;
}

namespace nextpilot::plotscope::ext::automotive {

// 注册 automotive 的 view provider（automotive.canBrowser → CanBrowserPanel）。
// 幂等：先到者为准。services / appCtx / executor 任一缺失返回 false，不硬造。
bool installAutomotiveViewFactories(instantiation::InstantiationService *services, context::AppContext *appCtx,
                                    intent::IIntentExecutor *executor);

} // namespace nextpilot::plotscope::ext::automotive
