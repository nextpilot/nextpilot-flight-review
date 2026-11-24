#include "Region.h"

#include <QHash>

namespace nextpilot::plotscope::registry
{

namespace
{

// 旧方位名 → 逻辑区域的一次性迁移表（D14）。
// 新增代码不写旧名；这张表只服务 JSON / IPC / 旧持久化读取边界。
const QHash<QString, Region> &aliasMap()
{
    static const QHash<QString, Region> map = {
        {QStringLiteral("dock.left"), Region::PrimarySidebar},
        {QStringLiteral("dock.right"), Region::SecondarySidebar},
        {QStringLiteral("dock.bottom"), Region::Panel},
        {QStringLiteral("dock.diagnostics"), Region::Panel},
        {QStringLiteral("canvas.center"), Region::Editor},
        // canvas.tool 已取消：工具归 Editor 视图的 view/title，无独立区域。
        {QStringLiteral("canvas.tool"), Region::Editor},
    };
    return map;
}

} // namespace

QString regionName(Region region)
{
    switch (region)
    {
    case Region::ActivityBar:
        return QStringLiteral("activitybar");
    case Region::PrimarySidebar:
        return QStringLiteral("primarySidebar");
    case Region::SecondarySidebar:
        return QStringLiteral("secondarySidebar");
    case Region::Panel:
        return QStringLiteral("panel");
    case Region::Editor:
        return QStringLiteral("editor");
    }
    return QStringLiteral("editor");
}

Region toRegion(const QString &name)
{
    const QString key = name.trimmed();
    if (key == QStringLiteral("activitybar"))
        return Region::ActivityBar;
    if (key == QStringLiteral("primarySidebar"))
        return Region::PrimarySidebar;
    if (key == QStringLiteral("secondarySidebar"))
        return Region::SecondarySidebar;
    if (key == QStringLiteral("panel"))
        return Region::Panel;
    if (key == QStringLiteral("editor"))
        return Region::Editor;

    const QHash<QString, Region> &aliases = aliasMap();
    const auto it = aliases.constFind(key);
    if (it != aliases.constEnd())
        return it.value();

    // 未知名兜底 Editor；不做运行期白名单校验（D17）。
    return Region::Editor;
}

} // namespace nextpilot::plotscope::registry
