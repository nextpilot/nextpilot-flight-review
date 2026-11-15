#include "PlotModule.h"

namespace ps::components
{

void registerPlotContributions(Registry &registry)
{
    registry.addRibbon(RibbonContribution{QStringLiteral("绘图"), {}, {},
        {{QStringLiteral("页面"),
             {{QStringLiteral("page.new"), QStringLiteral("新建页"), {}, true},
              {QStringLiteral("page.addCanvas"), QStringLiteral("添加画布"), {}, true},
              {QStringLiteral("page.splitHorizontal"), QStringLiteral("向右拆分"), {}, true},
              {QStringLiteral("page.splitVertical"), QStringLiteral("向下拆分"), {}, true}}},
         {QStringLiteral("画布布局"), {}, QStringLiteral("layoutPicker")},
         {QStringLiteral("显示"), {}, QStringLiteral("viewToggles")},
         {QStringLiteral("图表类型"), {}, QStringLiteral("plotGallery")}},
        2});
}

} // namespace ps::components
