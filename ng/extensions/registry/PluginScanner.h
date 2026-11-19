#pragma once

#include "ExtensionRegistry.h"

#include <QString>
#include <QStringList>

namespace nextpilot::plotscope::ext
{

// 外部插件发现结果（供 CLI / 诊断输出）。
struct PluginScanReport
{
    QStringList discovered; // 发现的候选插件文件名
    QStringList errors;     // 校验/加载失败说明
    int registered = 0;     // 成功注册进工具箱的驱动数
};

// 扫描外部插件目录，把发现的格式驱动注册进同一个 registry。
//
// 设计约定（本轮只搭骨架，不实际 dlopen）：
//   - 只有外部插件走动态扫描；内置格式走 registerBuiltinFormats 静态自注册。
//   - 插件导出统一的 C ABI 工厂（见下），返回 IMessageParser*；宿主校验 ABI 版本后接管。
//   - 加载失败（缺符号/ABI 不符/重复 id）单独记入 report.errors，不影响其他插件，
//     更不影响内置格式——这是"插件互不牵连"的隔离保证。
//
// 后续启用真实加载只需实现 loadOne()，扫描框架与注册表接线无需改动。
PluginScanReport scanExternalPlugins(nextpilot::plotscope::core::ExtensionRegistry &registry,
                                     const QString &directory);

} // namespace nextpilot::plotscope::ext
