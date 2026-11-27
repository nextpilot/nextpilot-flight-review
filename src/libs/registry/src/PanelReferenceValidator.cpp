#include "PanelReferenceValidator.h"

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
                                   "命中面 = views 声明 IViewsService::viewForId ∪ "
                                   "customEditors 声明 IEditorService::customEditors()。")
                        .arg(type, source);
    return issue;
}
} // namespace

QList<PanelReferenceIssue> validatePanelReferences(const QList<ActivitySpec> &activities,
                                                   const QList<ProductPanelPlacement> &placements,
                                                   const QStringList &knownViewIds,
                                                   const QStringList &customEditorViewTypes)
{
    QList<PanelReferenceIssue> issues;

    auto check = [&issues, &knownViewIds, &customEditorViewTypes](const QString &source, const QString &type) {
        if (type.isEmpty() || knownViewIds.contains(type) || customEditorViewTypes.contains(type))
            return;
        issues.append(makeIssue(source, type));
    };

    // 1) Activity Bar 入口关联的左栏 section 视图与中央编辑器。
    for (const ActivitySpec &activity : activities)
    {
        for (const QString &type : activity.panelTypes)
            check(QStringLiteral("Activity \"%1\" panelTypes").arg(activity.id), type);
        check(QStringLiteral("Activity \"%1\" centralEditor").arg(activity.id), activity.centralEditor);
    }

    // 2) 产品默认布局占位。
    for (const ProductPanelPlacement &placement : placements)
        check(QStringLiteral("产品默认布局"), placement.viewId);

    return issues;
}

QList<PanelReferenceIssue> validatePanelReferences(const UiSlotHub &slotHub,
                                                   const QList<ProductPanelPlacement> &placements,
                                                   const QStringList &knownViewIds,
                                                   const QStringList &customEditorViewTypes)
{

    return validatePanelReferences(slotHub.activities(), placements, knownViewIds, customEditorViewTypes);
}

} // namespace nextpilot::plotscope::registry
