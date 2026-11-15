#include "DataSourceModule.h"

namespace ps::components
{

void registerDataSourceContributions(Registry &registry)
{
    registry.addDock(DockContribution{QStringLiteral("dock.src"), QStringLiteral("数据源"),
                                      Qt::LeftDockWidgetArea, true, 0, nullptr});

    registry.addRibbon(RibbonContribution{QStringLiteral("数据"), {}, {},
        {{QStringLiteral("常用"),
             {{QStringLiteral("data.quickOpen"), QStringLiteral("快速打开"), {}, true},
              {QStringLiteral("data.recent"), QStringLiteral("最近"), {}, false},
              {QStringLiteral("data.exportSnapshot"), QStringLiteral("导出快照"), {}, false},
              {QStringLiteral("data.reload"), QStringLiteral("重载"), {}, false}}},
         {QStringLiteral("来源"), {}, QStringLiteral("sourceGallery")}},
        1});
}

} // namespace ps::components
