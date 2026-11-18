#pragma once

#include "ExtensionRegistry.h"

// 工具箱的"内置格式自注册"入口。
//
// app 启动期只调用 registerBuiltinFormats(registry)，完全不认识 CSV/ULog 这些
// 具体类——新增一种内置格式，只需在这里加一行注册，宿主与器官零改动。
//
// 外部插件走另一条路径：由 PluginScanner 扫描动态库、校验 ABI 后，把插件提供的
// 驱动注册进同一个 registry（本轮预留接口，不实际加载）。
namespace ng::ext
{
void registerBuiltinFormats(ng::core::ExtensionRegistry &registry);
}
