#include "UiSlotRegistry.h"

#include <algorithm>

namespace ng::core
{

void UiSlotRegistry::declareSlot(const QString &area, const QString &label, const QString &owner)
{
    auto it = std::find_if(m_slots.begin(), m_slots.end(),
                           [&area](const UiSlot &s) { return s.area == area; });
    if (it != m_slots.end())
        return; // 已声明，先到者为准
    m_slots.append(UiSlot{area, label, owner});
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

QList<BoundUiAction> UiSlotRegistry::actionsIn(const QString &area) const
{
    return m_bound.value(area, {});
}

int UiSlotRegistry::boundActionCount() const
{
    int n = 0;
    for (auto it = m_bound.constBegin(); it != m_bound.constEnd(); ++it)
        n += it.value().size();
    return n;
}

QList<BoundUiAction> UiSlotRegistry::unboundActions() const
{
    return m_unbound;
}

} // namespace ng::core
