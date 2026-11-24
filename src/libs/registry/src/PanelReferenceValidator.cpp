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
    issue.message = QStringLiteral("面板类型 \"%1\" 未在工厂表注册（来源: %2）；"
                                   "可用类型见 PanelFactoryHub::types()，工厂由视图层 "
                                   "PS_REGISTER_PANEL / PS_REGISTER_PANEL_T 自注册。")
                        .arg(type, source);
    return issue;
}
} // namespace

QList<PanelReferenceIssue> validatePanelReferences(const ToolboxHub &toolbox,
                                                   const UiSlotHub &slotHub,
                                                   const QList<ProductPanelPlacement> &placements)
{
    QList<PanelReferenceIssue> issues;
    const QStringList known = PanelFactoryHub::types();

    auto check = [&issues, &known](const QString &source, const QString &type) {
        if (type.isEmpty() || known.contains(type))
            return;
        issues.append(makeIssue(source, type));
    };

    // 1) 包声明的领域面板：provider 由 mount() 自动写入，诊断可直接归到包。
    for (const PanelSpec &panel : toolbox.panels.values())
        check(QStringLiteral("面板声明（provider=%1）").arg(panel.provider), panel.id);

    // 2) Activity Bar 入口关联的左栏 section 面板与中央面板。
    for (const ActivitySpec &activity : slotHub.activities())
    {
        for (const QString &type : activity.panelTypes)
            check(QStringLiteral("Activity \"%1\" panelTypes").arg(activity.id), type);
        check(QStringLiteral("Activity \"%1\" centralPanel").arg(activity.id), activity.centralPanel);
    }

    // 3) 产品默认布局占位。
    for (const ProductPanelPlacement &placement : placements)
        check(QStringLiteral("产品默认布局 %1").arg(regionName(placement.region)), placement.panelType);

    return issues;
}

} // namespace nextpilot::plotscope::registry
