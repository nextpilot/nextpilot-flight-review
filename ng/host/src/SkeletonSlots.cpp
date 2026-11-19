#include "SkeletonSlots.h"

using namespace ng::core;

namespace ng::host
{

void declareSkeletonSlots(UiSlotRegistry &registry)
{
    // app 骨架的标准插槽。
    //
    // 声明者统一是 "app.skeleton"：它们不属于任何可卸载的模块或知识包，
    // 装卸它们不影响骨架（架构上：app 骨架与业务 UI 彻底分离）。
    const QString owner = QStringLiteral("app.skeleton");

    // --- ribbon：页签 -> 分组 ---
    registry.declareSlot(QStringLiteral("ribbon.page.home"), QStringLiteral("主页"), owner,
                         SlotKind::RibbonPage);
    registry.declareSlot(QStringLiteral("ribbon.group.home.data"), QStringLiteral("数据"), owner,
                         SlotKind::RibbonGroup, QStringLiteral("ribbon.page.home"));
    registry.declareSlot(QStringLiteral("ribbon.group.home.view"), QStringLiteral("视图"), owner,
                         SlotKind::RibbonGroup, QStringLiteral("ribbon.page.home"));
    registry.declareSlot(QStringLiteral("ribbon.group.home.appearance"), QStringLiteral("外观"), owner,
                         SlotKind::RibbonGroup, QStringLiteral("ribbon.page.home"));
    // 图型库：12 种绘图类型（曲线/地图3D/仪表）用 SARibbonGallery 呈现，
    // 抄老 PlottingRibbon 的 UI 体验；纯数据声明在此，缩略图由 GUI 层画。
    registry.declareSlot(QStringLiteral("ribbon.group.home.plottype"), QStringLiteral("图型"), owner,
                         SlotKind::RibbonGroup, QStringLiteral("ribbon.page.home"));
    // 来源库：各数据格式/实时源用 gallery 选择（抄老 DataSourceRibbon 的"来源"组）。
    registry.declareSlot(QStringLiteral("ribbon.group.home.sources"), QStringLiteral("来源"), owner,
                         SlotKind::RibbonGroup, QStringLiteral("ribbon.page.home"));

    // --- 停靠区 ---
    registry.declareSlot(QStringLiteral("dock.left"), QStringLiteral("数据集"), owner,
                         SlotKind::DockPanel);
    registry.declareSlot(QStringLiteral("dock.right"), QStringLiteral("属性"), owner,
                         SlotKind::DockPanel);

    // --- 中央画布 ---
    registry.declareSlot(QStringLiteral("canvas.center"), QStringLiteral("画布"), owner,
                         SlotKind::Canvas);
    // 画布工具区：画布上的工具入口（ARCHITECTURE.md §4 器官表："画布工具"）。
    // 它是**画布的一部分**，所以 kind = Canvas 而不是默认的 DockPanel。
    registry.declareSlot(QStringLiteral("canvas.tool"), QStringLiteral("画布工具"), owner,
                         SlotKind::Canvas);

    // --- 菜单栏 ---
    registry.declareSlot(QStringLiteral("menu.plot"), QStringLiteral("绘图"), owner,
                         SlotKind::Menu);
}

} // namespace ng::host