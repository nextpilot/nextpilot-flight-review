#pragma once

#include "ExtensionRegistry.h"

// 内置知识包的自注册入口。
//
// app 启动期只调用 registerBuiltinPacks(registry)，完全不认识 CSV/ULog 或任何
// 具体贡献类型——新增一个领域的知识包、或往某包加一类贡献，只在这里装配，
// 宿主与器官零改动。
//
// 外部插件走另一条路径：由 PluginScanner 扫描动态库、校验 ABI 后，把插件提供的
// 整个 ToolPack registerPack 进同一个 registry。
namespace ng::ext
{

// 装配全部内置知识包（通用 / 飞控 / 汽车 / 机器人 / 卫导），一次注册到工具箱。
void registerBuiltinPacks(ng::core::ExtensionRegistry &registry);

// 向后兼容：历史上只登记格式驱动的入口，现等价于 registerBuiltinPacks。
inline void registerBuiltinFormats(ng::core::ExtensionRegistry &registry)
{
    registerBuiltinPacks(registry);
}

} // namespace ng::ext
