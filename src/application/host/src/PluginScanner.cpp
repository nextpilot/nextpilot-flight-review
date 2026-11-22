#include "PluginScanner.h"

#include <QDir>
#include <QFileInfo>

namespace nextpilot::plotscope::ext
{

// 插件目录约定：以 "psplug_" 开头、平台动态库后缀结尾的文件视为候选。
static QStringList pluginSuffixes()
{
#if defined(Q_OS_WIN)
    return {QStringLiteral("dll")};
#elif defined(Q_OS_MACOS)
    return {QStringLiteral("dylib")};
#else
    return {QStringLiteral("so")};
#endif
}

PluginScanReport scanExternalPlugins(nextpilot::plotscope::registry::ToolboxHub &registry,
                                     const QString &directory)
{
    Q_UNUSED(registry); // 本轮不实际加载，保留接线位置
    PluginScanReport report;
    if (directory.isEmpty())
        return report;

    QDir dir(directory);
    if (!dir.exists())
        return report;

    const QStringList filters = [] {
        QStringList out;
        for (const QString &s : pluginSuffixes())
            out << QStringLiteral("psplug_*.%1").arg(s);
        return out;
    }();
    const QStringList found = dir.entryList(filters, QDir::Files | QDir::Readable);

    for (const QString &name : found)
    {
        report.discovered.append(name);
        // TODO(plugin-abi, M3): 外部扩展实装点。启用时在此：
        //   1) QLibrary / QPluginLoader 加载该文件；
        //   2) 解析 ps_plugin_abi() 校验版本，不符则 errors << "ABI 不符" 并 continue；
        //   3) 调用工厂取得 IMessageParser*，用 unique_ptr 接管；
        //   4) registry.registerDriver(std::move(driver))，失败则 errors << "id 冲突"。
        // 单个插件失败只记 errors，不中断其她插件，也不触碰内置格式。
        report.errors.append(QStringLiteral("%1: 动态加载尚未启用（本轮仅预留入口）").arg(name));
    }
    return report;
}

} // namespace nextpilot::plotscope::ext
