#include "SkeletonSlots.h"

using namespace nextpilot::plotscope::core;

namespace nextpilot::plotscope::host
{

void declareSkeletonSlots(UiSlotRegistry &registry)
{
    // app 骨架的标准插槽。
    // 声明者统一是 "app.skeleton"：它们不属于任何可卸载的模块或知识包。
    //
    // Ribbon 按老代码/用户要求分 8 页：开始、文件、数据、绘图、脚本、协议、分析、视图。
    // 每页是 RibbonPage，下面挂若干 RibbonGroup；各 module/工具箱把 action 填进对应页的 group。
    // order 决定页签顺序（UiSlotRegistry::slotsOfKind/childrenOf 按 order 升序）。
    const QString owner = QStringLiteral("app.skeleton");

    auto page = [&](const QString &area, const QString &label, int order) {
        registry.declareSlot(area, label, owner, SlotKind::RibbonPage, QString(), order);
    };
    auto group = [&](const QString &area, const QString &label, const QString &parent, int order = 0) {
        registry.declareSlot(area, label, owner, SlotKind::RibbonGroup, parent, order);
    };

    // --- 8 个 ribbon 页 ---
    page(QStringLiteral("ribbon.page.home"),     QStringLiteral("开始"), 0);
    page(QStringLiteral("ribbon.page.file"),     QStringLiteral("文件"), 1);
    page(QStringLiteral("ribbon.page.data"),     QStringLiteral("数据"), 2);
    page(QStringLiteral("ribbon.page.plot"),     QStringLiteral("绘图"), 3);
    page(QStringLiteral("ribbon.page.script"),   QStringLiteral("脚本"), 4);
    page(QStringLiteral("ribbon.page.protocol"), QStringLiteral("协议"), 5);
    page(QStringLiteral("ribbon.page.analysis"), QStringLiteral("分析"), 6);
    page(QStringLiteral("ribbon.page.view"),     QStringLiteral("视图"), 7);

    // 开始页：常用入口 + 外观（老代码无此页，按用户要求补，聚合最高频操作）。
    group(QStringLiteral("ribbon.group.home.data"), QStringLiteral("常用"),
          QStringLiteral("ribbon.page.home"));
    group(QStringLiteral("ribbon.group.home.appearance"), QStringLiteral("外观"),
          QStringLiteral("ribbon.page.home"));
    // 兼容保留：home 下这两个 group 历史上承载图型/来源 gallery。
    // 现在真正的图型库在绘图页、来源库在数据页；这里保留空槽只为旧测试/插件的 area 引用不悬空。
    group(QStringLiteral("ribbon.group.home.plottype"), QStringLiteral("图型"),
          QStringLiteral("ribbon.page.home"));
    group(QStringLiteral("ribbon.group.home.view"), QStringLiteral("画布"),
          QStringLiteral("ribbon.page.home"));
    group(QStringLiteral("ribbon.group.home.sources"), QStringLiteral("来源"),
          QStringLiteral("ribbon.page.home"));

    // 文件页：打开/布局/导出/截图/帮助。
    group(QStringLiteral("ribbon.group.file.document"), QStringLiteral("文件"),
          QStringLiteral("ribbon.page.file"));
    group(QStringLiteral("ribbon.group.file.export"), QStringLiteral("导出"),
          QStringLiteral("ribbon.page.file"));
    group(QStringLiteral("ribbon.group.file.help"), QStringLiteral("帮助"),
          QStringLiteral("ribbon.page.file"));

    // 数据页：常用 + 来源 gallery + 调试。
    group(QStringLiteral("ribbon.group.data.common"), QStringLiteral("常用"),
          QStringLiteral("ribbon.page.data"));
    group(QStringLiteral("ribbon.group.data.sources"), QStringLiteral("来源"),
          QStringLiteral("ribbon.page.data"));
    group(QStringLiteral("ribbon.group.data.debug"), QStringLiteral("调试"),
          QStringLiteral("ribbon.page.data"));

    // 绘图页：页面/布局/显示/画布操作 + 图型 gallery + 曲线与轴操作。
    group(QStringLiteral("ribbon.group.plot.page"), QStringLiteral("页面"),
          QStringLiteral("ribbon.page.plot"));
    group(QStringLiteral("ribbon.group.plot.layout"), QStringLiteral("布局"),
          QStringLiteral("ribbon.page.plot"));
    group(QStringLiteral("ribbon.group.plot.display"), QStringLiteral("显示"),
          QStringLiteral("ribbon.page.plot"));
    group(QStringLiteral("ribbon.group.plot.tool"), QStringLiteral("画布操作"),
          QStringLiteral("ribbon.page.plot"));
    group(QStringLiteral("ribbon.group.plot.type"), QStringLiteral("图表类型"),
          QStringLiteral("ribbon.page.plot"));
    group(QStringLiteral("ribbon.group.plot.series"), QStringLiteral("曲线与轴"),
          QStringLiteral("ribbon.page.plot"));
    group(QStringLiteral("ribbon.group.plot.action"), QStringLiteral("操作"),
          QStringLiteral("ribbon.page.plot"));

    // 脚本页：控制台 + 运行（老代码无此页，按用户要求补）。
    group(QStringLiteral("ribbon.group.script.console"), QStringLiteral("控制台"),
          QStringLiteral("ribbon.page.script"));
    group(QStringLiteral("ribbon.group.script.run"), QStringLiteral("运行"),
          QStringLiteral("ribbon.page.script"));

    // 协议页：编辑器 + 检查。
    group(QStringLiteral("ribbon.group.protocol.editor"), QStringLiteral("编辑器"),
          QStringLiteral("ribbon.page.protocol"));
    group(QStringLiteral("ribbon.group.protocol.debug"), QStringLiteral("检查"),
          QStringLiteral("ribbon.page.protocol"));

    // 分析页：应用库 gallery。
    group(QStringLiteral("ribbon.group.analysis.apps"), QStringLiteral("应用库"),
          QStringLiteral("ribbon.page.analysis"));

    // 视图页：面板开关 + 外观。
    group(QStringLiteral("ribbon.group.view.panels"), QStringLiteral("面板"),
          QStringLiteral("ribbon.page.view"));
    group(QStringLiteral("ribbon.group.view.appearance"), QStringLiteral("外观"),
          QStringLiteral("ribbon.page.view"));

    // --- 停靠区 ---
    registry.declareSlot(QStringLiteral("dock.left"), QStringLiteral("数据集"), owner,
                         SlotKind::DockPanel);
    registry.declareSlot(QStringLiteral("dock.right"), QStringLiteral("属性"), owner,
                         SlotKind::DockPanel);
    registry.declareSlot(QStringLiteral("dock.bottom"), QStringLiteral("日志"), owner,
                         SlotKind::DockPanel);

    // --- 中央画布 ---
    registry.declareSlot(QStringLiteral("canvas.center"), QStringLiteral("画布"), owner,
                         SlotKind::Canvas);
    registry.declareSlot(QStringLiteral("canvas.tool"), QStringLiteral("画布工具"), owner,
                         SlotKind::Canvas);

    // --- 菜单栏 ---
    registry.declareSlot(QStringLiteral("menu.plot"), QStringLiteral("绘图"), owner,
                         SlotKind::Menu);
}

} // namespace nextpilot::plotscope::host
