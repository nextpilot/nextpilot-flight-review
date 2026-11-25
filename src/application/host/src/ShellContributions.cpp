#include "ShellContributions.h"

#include "ISlotsService.h"

namespace nextpilot::plotscope::host
{

namespace
{

// 声明清单的唯一事实源（裁定 52442006 Step 4）：两个通道（UiSlotHub 直连=host
// 内部；ISlotsService=壳）共用同一组回调，避免双份清单漂移。
template <typename PageFn, typename GroupFn, typename ActivityFn>
void applyShellContributions(PageFn page, GroupFn group, ActivityFn activity)
{
    // 声明者统一是 "app.skeleton"：它们不属于任何可卸载的模块或扩展包。
    const QString owner = QStringLiteral("app.skeleton");

    // --- 壳级页面 ---
    // 开始页（order 0）：产品级聚合落地页，暂由壳声明；等出现独立 home 模块再迁。
    // 边界：这里只给**容器**，页内具体业务条目仍由各模块贡献（如 DataModule 把
    // 打开/最近挂到 home.data 组），壳不预置业务动作。
    page(QStringLiteral("ribbon.page.home"), QStringLiteral("开始"), 0);
    group(QStringLiteral("ribbon.group.home.data"), QStringLiteral("常用"),
          QStringLiteral("ribbon.page.home"), 0);

    // 文件页（order 1）：文件 / 导出 / 截图 / 帮助四组。
    page(QStringLiteral("ribbon.page.file"), QStringLiteral("文件"), 1);
    group(QStringLiteral("ribbon.group.file.document"), QStringLiteral("文件"),
          QStringLiteral("ribbon.page.file"), 0);
    group(QStringLiteral("ribbon.group.file.export"), QStringLiteral("导出"),
          QStringLiteral("ribbon.page.file"), 1);
    group(QStringLiteral("ribbon.group.file.screenshot"), QStringLiteral("截图"),
          QStringLiteral("ribbon.page.file"), 2);
    group(QStringLiteral("ribbon.group.file.help"), QStringLiteral("帮助"),
          QStringLiteral("ribbon.page.file"), 3);

    // 视图页（order 8）：面板开关 + 外观。
    page(QStringLiteral("ribbon.page.view"), QStringLiteral("视图"), 8);
    group(QStringLiteral("ribbon.group.view.panels"), QStringLiteral("面板"),
          QStringLiteral("ribbon.page.view"), 0);
    group(QStringLiteral("ribbon.group.view.appearance"), QStringLiteral("外观"),
          QStringLiteral("ribbon.page.view"), 1);

    // 诊断入口归外壳（原 MainWindow 声明，A8b-M2 迁此）。
    nextpilot::plotscope::registry::ActivitySpec diagnostics;
    diagnostics.id = QStringLiteral("diagnostics");
    diagnostics.label = QStringLiteral("诊断");
    diagnostics.icon = QStringLiteral("diagnostics");
    diagnostics.order = 3;
    diagnostics.ribbonPage = QStringLiteral("ribbon.page.view");
    diagnostics.provider = owner;
    activity(diagnostics);
}

} // namespace

void declareShellContributions(nextpilot::plotscope::registry::UiSlotHub &registry)
{
    using registry::SlotKind;
    applyShellContributions(
        [&registry](const QString &id, const QString &label, int order) {
            registry.declareSlot(id, label, QStringLiteral("app.skeleton"), SlotKind::RibbonPage,
                                 QString(), order);
        },
        [&registry](const QString &id, const QString &label, const QString &parent, int order) {
            registry.declareSlot(id, label, QStringLiteral("app.skeleton"), SlotKind::RibbonGroup,
                                 parent, order);
        },
        [&registry](const registry::ActivitySpec &spec) { registry.registerActivity(spec); });
}

void declareShellContributions(nextpilot::plotscope::workbench::ISlotsService &slotSvc)
{
    using registry::SlotKind;
    applyShellContributions(
        [&slotSvc](const QString &id, const QString &label, int order) {
            slotSvc.declareSlot(id, label, QStringLiteral("app.skeleton"), SlotKind::RibbonPage,
                                QString(), order);
        },
        [&slotSvc](const QString &id, const QString &label, const QString &parent, int order) {
            slotSvc.declareSlot(id, label, QStringLiteral("app.skeleton"), SlotKind::RibbonGroup,
                                parent, order);
        },
        [&slotSvc](const registry::ActivitySpec &spec) { slotSvc.registerActivity(spec); });
}

} // namespace nextpilot::plotscope::host
