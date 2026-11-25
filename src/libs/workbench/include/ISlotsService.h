#pragma once

// A8b-M2 裁定 52442006 Step 1：壳（gui/shell）对槽位表的**读写统一走本接口**，
// 实现唯一是 registry::UiSlotHub（装配层 SlotsServiceAdapter 一行转发），壳不再
// include UiSlotHub.h、不再持有 registry::UiSlotHub&（Step 4 判据：
// `git grep UiSlotHub -- src/application/gui` = 0）。
// 方法集 = 壳实际用到面（灌回 5 + 读回 6），纯转发不加语义：
//   灌回：declareSlot / registerActivity / bindLocal / bindActionProvider /
//         registerModuleThemeToken
//   读回：activities / activity(id) / slotsOfKind / childrenOf / actionsIn /
//         allSlots（registerSlotCommands 用，监工清单 10 个 + 此 1 个）

#include "UiSlotTypes.h" // SlotKind / UiSlot / BoundUiAction

#include "Contribution.h" // UiAction / ActivitySpec / ThemeTokenSpec

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace nextpilot::plotscope::workbench
{

class ISlotsService
{
public:
    virtual ~ISlotsService() = default;

    // --- 灌回（装配层 installModuleContributions / 壳骨架声明）---
    virtual void declareSlot(const QString &area, const QString &label, const QString &owner,
                             registry::SlotKind kind = registry::SlotKind::RibbonGroup,
                             const QString &parentSlotId = QString(), int order = 0,
                             const QString &when = QString()) = 0;
    virtual void registerActivity(registry::ActivitySpec activity) = 0;
    virtual void bindLocal(const QString &area, const registry::UiAction &action,
                           const QString &provider) = 0;
    virtual void bindActionProvider(const QString &area, const QString &provider,
                                    std::function<QList<registry::UiAction>()> fn) = 0;
    virtual void registerModuleThemeToken(const registry::ThemeTokenSpec &token) = 0;
    // 模块声明的语义色 token 读回（壳/装配层汇总主题用；同 id 先到者优先）。
    virtual QList<registry::ThemeTokenSpec> moduleThemeTokens() const = 0;

    // --- 读回（壳渲染）---
    virtual QList<registry::ActivitySpec> activities() const = 0;
    virtual const registry::ActivitySpec *activity(const QString &id) const = 0;
    virtual QList<registry::UiSlot> slotsOfKind(registry::SlotKind kind) const = 0;
    virtual QList<registry::UiSlot> childrenOf(const QString &parentSlotId) const = 0;
    virtual QList<registry::BoundUiAction> actionsIn(const QString &area) const = 0;
    virtual QList<registry::UiSlot> allSlots() const = 0;
};

// 纯接口（无 QObject 基类）：registerSingleton 存接口地址，getService static_cast 恒安全。
} // namespace nextpilot::plotscope::workbench
