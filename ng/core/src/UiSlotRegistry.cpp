#include "UiSlotRegistry.h"

#include <algorithm>

namespace nextpilot::plotscope::core
{

void UiSlotRegistry::declareSlot(const QString &area, const QString &label, const QString &owner,
                                 SlotKind kind, const QString &parentArea, int order)
{
    auto it = std::find_if(m_slots.begin(), m_slots.end(),
                           [&area](const UiSlot &s) { return s.area == area; });
    if (it != m_slots.end())
        return; // 已声明，先到者为准
    m_slots.append(UiSlot{area, label, owner, kind, parentArea, order});
}

void UiSlotRegistry::refreshFrom(const ExtensionRegistry &registry)
{
    m_bound.clear();
    m_unbound.clear();
    // 预建 slot 集合，确保未声明插槽也能保持声明顺序
    for (const UiSlot &slot : m_slots)
        m_bound.insert(slot.area, {});

    for (IToolContribution *c : registry.contributions(ContributionKind::Ui))
    {
        auto *ui = dynamic_cast<IUiContribution *>(c);
        if (!ui)
            continue;
        for (const UiAction &action : ui->actions())
        {
            BoundUiAction bound{action, ui->provider()};
            if (m_bound.contains(action.area))
                m_bound[action.area].append(bound);
            else
                m_unbound.append(bound);
        }
    }
}

void UiSlotRegistry::bindLocal(const QString &area, const UiAction &action, const QString &provider)
{
    if (!area.isEmpty())
        m_local[area].append(BoundUiAction{action, provider});
}

void UiSlotRegistry::unbindProvider(const QString &provider)
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

QStringList UiSlotRegistry::areas() const
{
    QStringList out;
    for (const UiSlot &s : m_slots)
        out.append(s.area);
    return out;
}

QList<UiSlot> UiSlotRegistry::allSlots() const
{
    return m_slots;
}

QList<UiSlot> UiSlotRegistry::slotsOfKind(SlotKind kind) const
{
    QList<UiSlot> out;
    for (const UiSlot &s : m_slots)
        if (s.kind == kind)
            out.append(s);
    return out;
}

QList<UiSlot> UiSlotRegistry::childrenOf(const QString &parentArea) const
{
    QList<UiSlot> out;
    for (const UiSlot &s : m_slots)
        if (s.parentArea == parentArea)
            out.append(s);
    std::sort(out.begin(), out.end(),
              [](const UiSlot &a, const UiSlot &b) { return a.order < b.order; });
    return out;
}

const UiSlot *UiSlotRegistry::slot(const QString &area) const
{
    for (const UiSlot &s : m_slots)
        if (s.area == area)
            return &s;
    return nullptr;
}

QList<BoundUiAction> UiSlotRegistry::actionsIn(const QString &area) const
{
    QList<BoundUiAction> out = m_bound.value(area, {});
    out += m_local.value(area, {});
    return out;
}

int UiSlotRegistry::boundActionCount() const
{
    int n = 0;
    for (auto it = m_bound.constBegin(); it != m_bound.constEnd(); ++it)
        n += it.value().size();
    for (auto it = m_local.constBegin(); it != m_local.constEnd(); ++it)
        n += it.value().size();
    return n;
}

QList<BoundUiAction> UiSlotRegistry::unboundActions() const
{
    return m_unbound;
}

} // namespace nextpilot::plotscope::core
