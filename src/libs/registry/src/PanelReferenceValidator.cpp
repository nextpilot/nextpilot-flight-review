#include "PanelReferenceValidator.h"

#include "PanelFactoryHub.h"
#include "Region.h"

namespace nextpilot::plotscope::registry
{

namespace
{
PanelReferenceIssue makeIssue(const QString &source, const QString &type)
{
    PanelReferenceIssue issue;
    issue.source = source;
    issue.type = type;
    issue.message = QStringLiteral("面板引用 \"%1\" 未命中任何声明面（来源: %2）；"
                                   "三命中面 = 工厂表 PanelFactoryHub::types() ∪ views 声明 "
                                   "IViewsService::viewForId ∪ customEditors 声明 "
                                   "IEditorService::customEditors()。")
                        .arg(type, source);
    return issue;
}
} // namespace

QList<PanelReferenceIssue> validatePanelReferences(const ToolboxHub &toolbox, const UiSlotHub &slotHub,
                                                   const QList<ProductPanelPlacement> &placements,
                                                   const QStringList &knownViewIds,
                                                   const QStringList &customEditorViewTypes)
{
    return validatePanelReferences(toolbox.panels.values(), slotHub, placements, knownViewIds, customEditorViewTypes);
}

QList<PanelReferenceIssue> validatePanelReferences(const QList<PanelSpec> &panels,
                                                   const QList<ActivitySpec> &activities,
                                                   const QList<ProductPanelPlacement> &placements,
                                                   const QStringList &knownViewIds,
                                                   const QStringList &customEditorViewTypes)
{
    QList<PanelReferenceIssue> issues;
    const QStringList known = PanelFactoryHub::types();

    auto check = [&issues, &known, &knownViewIds, &customEditorViewTypes](const QString &source, const QString &type) {
        if (type.isEmpty() || known.contains(type) || knownViewIds.contains(type) ||
            customEditorViewTypes.contains(type))
            return;
        issues.append(makeIssue(source, type));
    };

    for (const PanelSpec &panel : panels)
        check(QStringLiteral("面板声明（provider=%1）").arg(panel.provider), panel.id);

    for (const ActivitySpec &activity : activities)
    {
        for (const QString &type : activity.panelTypes)
            check(QStringLiteral("Activity \"%1\" panelTypes").arg(activity.id), type);
        check(QStringLiteral("Activity \"%1\" centralEditor").arg(activity.id), activity.centralEditor);
    }

    for (const ProductPanelPlacement &placement : placements)
        check(QStringLiteral("产品默认布局 %1").arg(regionName(placement.region)), placement.viewId);

    return issues;
}

QList<PanelReferenceIssue> validatePanelReferences(const QList<PanelSpec> &panels, const UiSlotHub &slotHub,
                                                   const QList<ProductPanelPlacement> &placements,
                                                   const QStringList &knownViewIds,
                                                   const QStringList &customEditorViewTypes)
{
    QList<PanelReferenceIssue> issues;
    const QStringList known = PanelFactoryHub::types();

    auto check = [&issues, &known, &knownViewIds, &customEditorViewTypes](const QString &source, const QString &type) {
        if (type.isEmpty() || known.contains(type) || knownViewIds.contains(type) ||
            customEditorViewTypes.contains(type))
            return;
        issues.append(makeIssue(source, type));
    };

    // 1) 包声明的领域面板：provider 由 mount() 自动写入，诊断可直接归到包。
    for (const PanelSpec &panel : panels)
        check(QStringLiteral("面板声明（provider=%1）").arg(panel.provider), panel.id);

    // 2) Activity Bar 入口关联的左栏 section 面板与中央面板。
    for (const ActivitySpec &activity : slotHub.activities())
    {
        for (const QString &type : activity.panelTypes)
            check(QStringLiteral("Activity \"%1\" panelTypes").arg(activity.id), type);
        check(QStringLiteral("Activity \"%1\" centralEditor").arg(activity.id), activity.centralEditor);
    }

    // 3) 产品默认布局占位。
    for (const ProductPanelPlacement &placement : placements)
        check(QStringLiteral("产品默认布局 %1").arg(regionName(placement.region)), placement.viewId);

    return issues;
}

} // namespace nextpilot::plotscope::registry
