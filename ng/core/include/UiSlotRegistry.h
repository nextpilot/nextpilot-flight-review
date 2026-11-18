#pragma once

#include "IUiContribution.h"
#include "ExtensionRegistry.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace ng::core
{

// 一个 UI 插槽：由"器官/骨架"声明（容器），由工具箱的 UI 贡献填充（内容）。
//
// 例如 app 骨架声明 "menu.plot" / "dock.left" / "ribbon.home" / "canvas.tool"，
// 飞控知识包贡献一个 area="menu.plot" 的 "姿态曲线" 动作——运行时把它挂进去。
// 这样业务 UI 随知识包装卸，而 app 骨架、器官代码永不改动。
struct UiSlot
{
    QString area;   // 插槽标识
    QString label;  // 展示名
    QString owner;  // 声明者（模块/骨架 id），便于归属诊断
};

// 已绑定到某插槽的一个动作 + 它来自哪个知识包。
struct BoundUiAction
{
    UiAction action;
    QString provider; // pack id，用于按包撤销
};

// UI 插槽登记表。
//
// 纯数据：不持有 QWidget、不依赖 Qt Widgets，因此 headless 也能完整地
// 声明插槽、绑定贡献、查询"某插槽会得到哪些入口"。真正的控件创建发生在
// GUI 层（未来带 QWidget 的 UiContext 实现）读取这里的数据之后。
class UiSlotRegistry
{
public:
    // 器官/骨架声明一个插槽；同 area 重复声明以先到者为准。
    void declareSlot(const QString &area, const QString &label, const QString &owner);

    // 从工具箱重新扫描所有 IUiContribution，按 area 绑定到已声明插槽。
    // 每次知识包装卸后调用一次即可（O(贡献数)，与数据量无关）。
    void refreshFrom(const ExtensionRegistry &registry);

    // 查询。
    QStringList areas() const;
    QList<UiSlot> allSlots() const;
    QList<BoundUiAction> actionsIn(const QString &area) const;
    int boundActionCount() const;

    // 未找到归属插槽的贡献动作（诊断用：声明了但插槽不存在）。
    QList<BoundUiAction> unboundActions() const;

private:
    QList<UiSlot> m_slots;                       // 声明顺序
    QHash<QString, QList<BoundUiAction>> m_bound; // area -> 动作
    QList<BoundUiAction> m_unbound;
};

} // namespace ng::core
