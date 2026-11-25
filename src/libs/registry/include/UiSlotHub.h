#pragma once

#include "Contribution.h"
#include "ToolboxHub.h"
#include "RegistryNode.h"
#include "RegistryTree.h"
#include "UiSlotTypes.h" // SlotKind / UiSlot / BoundUiAction（A8b-M2 抽出，契约头共用）

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace nextpilot::plotscope::registry
{

// UI 插槽登记表。
//
// 纯数据：不持有 QWidget、不依赖 Qt Widgets，因此 headless 也能完整地
// 声明插槽、绑定贡献、查询"某插槽会得到哪些入口"。真正的控件创建发生在
// GUI 层读取这里的数据之后。
//
// 两条绑定来源：
//   1) 工具箱 uiActions 表（业务能力带来的入口）；
//   2) 模块在 GUI 环境下直接注册的 UiAction（app 自身的功能入口）。
class UiSlotHub : public RegistryNode
{
public:
    // 构造即上树（kind=UiSlot，name=UI插槽；M2.8 遗留5：推迟池收口）。
    explicit UiSlotHub(RegistryTree &tree) : RegistryNode(tree, RegistryKind::UiSlot, QStringLiteral("UI插槽")) {}

    // ---- RegistryNode 可观测：已声明插槽（骨架槽清单，diag.registries 可枚举）----
    int count() const override { return m_slots.size(); }
    QStringList keysAsStrings() const override { return areas(); }

    // 声明一个插槽；同 area 重复声明以先到者为准。
    // 注意：core 只提供这个机制，不认识"app 具体有哪些槽"——
    // 壳级页/组清单归 host 层 ShellContributions，业务页/组归各模块 onAttach。
    void declareSlot(const QString &area, const QString &label, const QString &owner,
                     SlotKind kind = SlotKind::RibbonGroup,
                     const QString &parentSlotId = QString(), int order = 0,
                     const QString &when = QString());

    // 从工具箱 uiActions 表重新扫描，按 area 绑定到已声明插槽。
    // 每次扩展包装卸后调用一次即可（O(贡献数)，与数据量无关）。
    void refreshFrom(const ToolboxHub &registry);

    // 模块直接登记的入口（不来自工具箱）。key 用于幂等覆盖。
    void bindLocal(const QString &area, const UiAction &action, const QString &provider);

    // 动作提供器：装配时实时回调取当前条目（MRU / 收藏等随运行状态变化的清单）。
    // 与一次性登记的固定条目不同：提供器是**声明式来源**，每次 ribbon 重建时重新取数，
    // 不再走"清空 provider 再全量重注册"的隐藏状态通道（契约 §3.3 / D10）。
    using ActionProvider = std::function<QList<UiAction>()>;
    void bindActionProvider(const QString &area, const QString &provider, ActionProvider fn);

    // Activity Bar 入口（数据、脚本、分析、诊断）。同 id 重复声明以先到者为准。
    void registerActivity(ActivitySpec activity);
    QList<ActivitySpec> activities() const;
    const ActivitySpec *activity(const QString &id) const;

    // 模块声明的语义色 token（A8b-M2 installContributions 灌回；壳内主题汇总用）。
    // 同 id 先到者为准；只存纯数据，不含 owner——主题合并只需要 id/fallback。
    void registerModuleThemeToken(const ThemeTokenSpec &token);
    QList<ThemeTokenSpec> moduleThemeTokens() const;

    // 清除某提供者（包或模块）登记的所有本地入口——卸载时调用。
    void unbindProvider(const QString &provider);

    // 查询。
    QStringList areas() const;
    QList<UiSlot> allSlots() const;
    QList<UiSlot> slotsOfKind(SlotKind kind) const;
    QList<UiSlot> childrenOf(const QString &parentSlotId) const;
    const UiSlot *slot(const QString &area) const;
    QList<BoundUiAction> actionsIn(const QString &area) const;
    int boundActionCount() const;

    // 未找到归属插槽的贡献动作（诊断用：声明了但插槽不存在）。
    QList<BoundUiAction> unboundActions() const;

private:
    QList<UiSlot> m_slots;                             // 声明顺序
    QHash<QString, QList<BoundUiAction>> m_bound;       // area -> 动作（工具箱来源）
    QList<BoundUiAction> m_unbound;
    QHash<QString, QList<BoundUiAction>> m_local;        // area -> 动作（模块本地来源）
    QHash<QString, QHash<QString, ActionProvider>> m_providers; // area -> providerId -> 提供器
    QList<ActivitySpec> m_activities;                   // 声明顺序；查询按 order 排序
    QList<ThemeTokenSpec> m_moduleThemeTokens;          // 模块声明顺序；同 id 先到者为准
};

} // namespace nextpilot::plotscope::registry
