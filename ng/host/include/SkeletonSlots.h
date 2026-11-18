#pragma once

#include "UiSlotRegistry.h"

namespace ng::host
{

// 声明 app 骨架的标准插槽：主页页签、数据/视图分组、左右停靠区、画布、
// 画布工具、绘图菜单——共 8 个。
//
// 归属：这是 **app 骨架的配置**（app 长什么样），不是 core 的机制。
// core 只提供"插槽注册表 + declareSlot 能力"，不认识"具体有哪些槽"。
// 因此把清单放在 host 层（GUI 与 headless 共同依赖的层），改槽清单
// 只动这里，不再改 core 文件。
//
// 单一来源：ModuleHost 构造函数是唯一的真实声明点；GUI 侧 MainWindow
// 在脱离宿主单独使用时可再调一次作幂等确认（同内容、先到者为准）。
void declareSkeletonSlots(ng::core::UiSlotRegistry &registry);

} // namespace ng::host