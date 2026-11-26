#pragma once

#include <QHash>
#include <QIcon>
#include <QString>

// 图标注册表（A2c 呈现侧三件之一）：**纯数据表**，谁声明谁注册，壳只查。
//
// 对位 VSCode：扩展在激活时把图标注册进 icon registry（`ThemeIcon` 的 id → 图标），
// 壳/ ribbon 只按 id 取 QIcon。差异登记 docs/develop/vscode-alignment.md §3（⚠️
// 仓内是静态单例表，不是 VSCode 的 IconContribution 全链）。
//
// 刻意**不造 IIconService**：壳与模块之间已经有 IViewsService / IEditorService 等
// 服务面，图标是纯呈现数据、无生命周期语义（注册即进程存续），平行入口只会
// 复制一份 service 样板（A2c 打回线 5）。PlotThumbnails 等绘制器在装配时把
// "plot/<kind>" 的 QIcon 注册进来；壳只 `IconRegistry::instance().icon(id)`。
namespace nextpilot::plotscope::gui {

class IconRegistry
{
public:
    static IconRegistry &instance();

    // 注册图标（幂等：先到者为准——重复注册同名 id 不覆盖，装配重入安全）。
    void registerIcon(const QString &id, const QIcon &icon);

    // 按 id 取图标；未注册返回空 QIcon（调用方自行回退主题图标）。
    QIcon icon(const QString &id) const;

private:
    IconRegistry() = default;
    QHash<QString, QIcon> m_icons;
};

} // namespace nextpilot::plotscope::gui
