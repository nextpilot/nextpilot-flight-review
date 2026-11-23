#pragma once

#include "Contribution.h"
#include "ToolboxHub.h"
#include "RegistryNode.h"
#include "RegistryTree.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace nextpilot::plotscope::registry
{

// 插槽的语义类别：决定 GUI 层把内容放到哪里、怎么呈现。
//
// 这是"插槽"从字符串升级为结构化描述的关键一步：插槽不再只是"一个名字"，
// 而是声明了它的**容器语义**（ribbon 页签 / 停靠面板 / 中央画布 / 菜单），
// 于是 GUI 层可以按类别自动布局，而贡献方只需说明"我要什么"。
enum class SlotKind
{
    RibbonPage,  // ribbon 的一个页签（下面再分组）
    RibbonGroup, // ribbon 页签内的一个分组
    DockPanel,   // 停靠面板（left/right/bottom）
    Canvas,      // 中央画布
    Menu         // 菜单栏下的一个菜单
};

// 一个 UI 插槽：由"app 骨架"声明（容器），由模块或工具箱贡献填充（内容）。
//
// 例如骨架声明：
//   ribbon.page.home  /  ribbon.group.home.data  /  dock.left  /  canvas.center
// 飞控包贡献一个 "姿态曲线" 按钮，填进 ribbon.group.home.data。
//
// 业务 UI 随扩展包装卸，而骨架与模块代码永不改动。
struct UiSlot
{
    QString area;   // 插槽标识（唯一）
    QString label;  // 展示名
    QString owner;  // 声明者（模块/骨架 id），便于归属诊断
    SlotKind kind = SlotKind::DockPanel;
    QString parentArea; // 上级插槽（ribbon group 的父页签等）；空=顶层
    int order = 0;       // 同级排序
};

// 已绑定到某插槽的一个动作 + 它来自哪个扩展包。
struct BoundUiAction
{
    UiAction action;
    QString provider; // pack id，用于按包撤销
};

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
    // 骨架槽清单归 host 层的 SkeletonSlots 声明。
    void declareSlot(const QString &area, const QString &label, const QString &owner,
                     SlotKind kind = SlotKind::DockPanel,
                     const QString &parentArea = QString(), int order = 0);

    // 从工具箱 uiActions 表重新扫描，按 area 绑定到已声明插槽。
    // 每次扩展包装卸后调用一次即可（O(贡献数)，与数据量无关）。
    void refreshFrom(const ToolboxHub &registry);

    // 模块直接登记的入口（不来自工具箱）。key 用于幂等覆盖。
    void bindLocal(const QString &area, const UiAction &action, const QString &provider);

    // Activity Bar 入口（数据、脚本、分析、诊断）。同 id 重复声明以先到者为准。
    void declareActivity(ActivitySpec activity);
    QList<ActivitySpec> activities() const;
    const ActivitySpec *activity(const QString &id) const;

    // 清除某提供者（包或模块）登记的所有本地入口——卸载时调用。
    void unbindProvider(const QString &provider);

    // 查询。
    QStringList areas() const;
    QList<UiSlot> allSlots() const;
    QList<UiSlot> slotsOfKind(SlotKind kind) const;
    QList<UiSlot> childrenOf(const QString &parentArea) const;
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
    QList<ActivitySpec> m_activities;                   // 声明顺序；查询按 order 排序
};

} // namespace nextpilot::plotscope::registry
