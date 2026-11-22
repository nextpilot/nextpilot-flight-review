#include "UiSlotHub.h"

#include <algorithm>

namespace nextpilot::plotscope::registry
{

void UiSlotHub::declareSlot(const QString &area, const QString &label, const QString &owner,
                                 SlotKind kind, const QString &parentArea, int order)
{
    auto it = std::find_if(m_slots.begin(), m_slots.end(),
                           [&area](const UiSlot &s) { return s.area == area; });
    if (it != m_slots.end())
        return; // 已声明，先到者为准
    m_slots.append(UiSlot{area, label, owner, kind, parentArea, order});
    Q_EMIT objectAdded(area); // M2.8：统一节点信号（带 key）
}

void UiSlotHub::refreshFrom(const ToolboxHub &registry)
{
    m_bound.clear();
    m_unbound.clear();
    // 预建 slot 集合，确保未声明插槽也能保持声明顺序
    for (const UiSlot &slot : m_slots)
        m_bound.insert(slot.area, {});

    // 内容全是数据：直接读 uiActions 表，归属（provider）就在条目里。
    for (const UiAction &action : registry.uiActions.values())
    {
        BoundUiAction bound{action, action.provider};
        if (m_bound.contains(action.area))
            m_bound[action.area].append(bound);
        else
            m_unbound.append(bound);
    }
}

void UiSlotHub::bindLocal(const QString &area, const UiAction &action, const QString &provider)
{
    if (!area.isEmpty())
        m_local[area].append(BoundUiAction{action, provider});
}

void UiSlotHub::unbindProvider(const QString &provider)
{
    for (auto it = m_local.begin(); it != m_local.end();)
    {
        auto &list = it.value();
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [&provider](const BoundUiAction &a) { return a.provider == provider; }),
                   list.end());
        if (list.isEmpty())
            it = m_local.erase(it);
        else
            ++it;
    }
}

QStringList UiSlotHub::areas() const
{
    QStringList out;
    for (const UiSlot &s : m_slots)
        out.append(s.area);
    return out;
}

QList<UiSlot> UiSlotHub::allSlots() const
{
    return m_slots;
}

QList<UiSlot> UiSlotHub::slotsOfKind(SlotKind kind) const
{
    QList<UiSlot> out;
    for (const UiSlot &s : m_slots)
        if (s.kind == kind)
            out.append(s);
    // 页签顺序按 order 显式可控，同权重按 label 稳定排列，与声明/装载顺序解耦。
    std::stable_sort(out.begin(), out.end(),
                     [](const UiSlot &a, const UiSlot &b) {
                         if (a.order != b.order)
                             return a.order < b.order;
                         return a.label < b.label;
                     });
    return out;
}

QList<UiSlot> UiSlotHub::childrenOf(const QString &parentArea) const
{
    QList<UiSlot> out;
    for (const UiSlot &s : m_slots)
        if (s.parentArea == parentArea)
            out.append(s);
    std::sort(out.begin(), out.end(),
              [](const UiSlot &a, const UiSlot &b) { return a.order < b.order; });
    return out;
}

const UiSlot *UiSlotHub::slot(const QString &area) const
{
    for (const UiSlot &s : m_slots)
        if (s.area == area)
            return &s;
    return nullptr;
}

QList<BoundUiAction> UiSlotHub::actionsIn(const QString &area) const
{
    QList<BoundUiAction> out = m_bound.value(area, {});
    out += m_local.value(area, {});
    // 组内动作顺序：先按 order 升序，同权重按 id 字典序，保证与模块/工具箱装载
    // 顺序完全解耦（顺序由 order 显式表达，不依赖数组序）。
    // gallery 的分组出现顺序由 ShellBinder 按这里返回的动作顺序聚合，因此也稳定。
    std::stable_sort(out.begin(), out.end(),
                     [](const BoundUiAction &a, const BoundUiAction &b) {
                         if (a.action.order != b.action.order)
                             return a.action.order < b.action.order;
                         return a.action.id < b.action.id;
                     });
    return out;
}

int UiSlotHub::boundActionCount() const
{
    int n = 0;
    for (auto it = m_bound.constBegin(); it != m_bound.constEnd(); ++it)
        n += it.value().size();
    for (auto it = m_local.constBegin(); it != m_local.constEnd(); ++it)
        n += it.value().size();
    return n;
}

QList<BoundUiAction> UiSlotHub::unboundActions() const
{
    return m_unbound;
}

} // namespace nextpilot::plotscope::registry
