#pragma once

// 壳级 Ribbon 贡献声明（契约 D13）。
//
// 解散原 SkeletonSlots 后的分工：
//   - 区域（ActivityBar / PrimarySidebar / SecondarySidebar / Panel / Editor）
//     是壳的代码常量，见 `registry::Region`，**不进** slot 注册表；
//   - slot 注册表只承载 Ribbon 页签与分组这两类有真实消费者的容器；
//   - 壳只保留自己的两页（文件、视图）与所属分组；其余业务页（开始/数据/绘图/
//     脚本/协议/分析/报告）由各模块在 `onAttach` 里经 `IUiContext::declareRibbonPage`
//     / `declareRibbonGroup` 自声明。
//
// 本文件是 app 骨架的配置（app 长什么样），不是 core 机制；core 只提供
// 插槽注册表与 declareSlot 能力。GUI 侧 MainWindow 脱离宿主单独使用时可再调一次
// 作幂等确认（同内容、先到者为准）。

#include "UiSlotHub.h"

namespace nextpilot::plotscope::host
{

void declareShellContributions(nextpilot::plotscope::registry::UiSlotHub &registry);

} // namespace nextpilot::plotscope::host
