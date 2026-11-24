#include "ShellContributions.h"

namespace nextpilot::plotscope::host
{

void declareShellContributions(nextpilot::plotscope::registry::UiSlotHub &registry)
{
    // 声明者统一是 "app.skeleton"：它们不属于任何可卸载的模块或扩展包。
    const QString owner = QStringLiteral("app.skeleton");

    auto page = [&](const QString &id, const QString &label, int order) {
        registry.declareSlot(id, label, owner,
                             nextpilot::plotscope::registry::SlotKind::RibbonPage,
                             QString(), order);
    };
    auto group = [&](const QString &id, const QString &label, const QString &parent, int order) {
        registry.declareSlot(id, label, owner,
                             nextpilot::plotscope::registry::SlotKind::RibbonGroup,
                             parent, order);
    };

    // --- 壳级两页 ---
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
}

} // namespace nextpilot::plotscope::host
