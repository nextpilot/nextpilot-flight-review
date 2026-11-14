#include "ShellComponent.h"

#include <QString>

namespace ps::components
{

ComponentManifest ShellComponent::manifest() const
{
    return {QStringLiteral("shell.core"), QStringLiteral("内置工作区"), QStringLiteral("1.0"),
            {QStringLiteral("ribbon")}};
}

void ShellComponent::contribute(Registry &registry, const HostServices &)
{

    registry.addDock(DockContribution{QStringLiteral("dock.src"), QStringLiteral("数据源"),
                                      Qt::LeftDockWidgetArea, true, 0, nullptr});
    registry.addDock(DockContribution{QStringLiteral("dock.prop"), QStringLiteral("属性面板"),
                                      Qt::RightDockWidgetArea, true, 1, nullptr});
    registry.addDock(DockContribution{QStringLiteral("dock.tree"), QStringLiteral("对象树"),
                                      Qt::RightDockWidgetArea, true, 2, nullptr});
    registry.addDock(DockContribution{QStringLiteral("dock.video"), QStringLiteral("视频同步"),
                                      Qt::RightDockWidgetArea, false, 3, nullptr});
    registry.addDock(DockContribution{QStringLiteral("dock.script"), QStringLiteral("脚本 · Lua 5.4"),
                                      Qt::RightDockWidgetArea, true, 4, nullptr});
    registry.addDock(DockContribution{QStringLiteral("dock.log"), QStringLiteral("日志"),
                                      Qt::BottomDockWidgetArea, true, 5, nullptr});

    registry.addRibbon(RibbonContribution{QStringLiteral("文件"), {}, {},
        {{QStringLiteral("文件"),
             {{QStringLiteral("file.open"), QStringLiteral("打开文件"), {}, true},
              {QStringLiteral("file.importLayout"), QStringLiteral("导入布局"), {}, true},
              {QStringLiteral("file.settings"), QStringLiteral("设置"), {}, true}}},
         {QStringLiteral("导出"),
             {{QStringLiteral("file.saveLayout"), QStringLiteral("保存布局"), {}, true},
              {QStringLiteral("file.exportSettings"), QStringLiteral("导出设置"), {}, false},
              {QStringLiteral("file.exportData"), QStringLiteral("导出数据"), {}, false}}},
         {QStringLiteral("截图"),
             {{QStringLiteral("file.screenshot"), QStringLiteral("截图当前页"), {}, true}}}},
        0});


    registry.addRibbon(RibbonContribution{QStringLiteral("数据"), {}, {},
        {{QStringLiteral("常用"),
             {{QStringLiteral("data.quickOpen"), QStringLiteral("快速打开"), {}, true},
              {QStringLiteral("data.recent"), QStringLiteral("最近"), {}, false},
              {QStringLiteral("data.exportSnapshot"), QStringLiteral("导出快照"), {}, false},
              {QStringLiteral("data.reload"), QStringLiteral("重载"), {}, false}}},
         {QStringLiteral("来源"), {}, QStringLiteral("sourceGallery")}},
        1});

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

    registry.addRibbon(RibbonContribution{QStringLiteral("脚本"), {}, {},
        {{QStringLiteral("Lua"),
             {{QStringLiteral("script.run"), QStringLiteral("运行"), {}, true},
              {QStringLiteral("script.format"), QStringLiteral("格式化"), {}, false}}}},
        3});

    registry.addRibbon(RibbonContribution{QStringLiteral("视图"), {}, {},
        {{QStringLiteral("面板"),
             {{QStringLiteral("view.toggleDock"), QStringLiteral("数据源"), QStringLiteral("dock.src"), true},
              {QStringLiteral("view.toggleDock"), QStringLiteral("属性"), QStringLiteral("dock.prop"), false},
              {QStringLiteral("view.toggleDock"), QStringLiteral("脚本"), QStringLiteral("dock.script"), false},
              {QStringLiteral("view.toggleDock"), QStringLiteral("日志"), QStringLiteral("dock.log"), false},
              {QStringLiteral("view.toggleDock"), QStringLiteral("视频"), QStringLiteral("dock.video"), false}}}},
        5});
}

} // namespace ps::components
