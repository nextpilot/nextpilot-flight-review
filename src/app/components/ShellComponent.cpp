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
