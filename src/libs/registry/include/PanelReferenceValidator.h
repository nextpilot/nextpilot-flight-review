#pragma once

// 面板引用启动期校验（验收 4）。
//
// 声明侧有三处引用面板类型 id：
//   1) 工具箱 PanelSpec::id（包声明的领域面板）；
//   2) ActivitySpec::panelTypes / centralPanel（Activity Bar 入口关联的面板）；
//   3) ProductSpec::defaultPanels[].panelType（产品默认布局占位）。
//
// 这些 id 必须在面板工厂表（PanelFactoryHub，视图层 PS_REGISTER_PANEL 自注册）命中；
// 任一未命中即产出一条 error 诊断：说明归属来源、未命中类型、可用类型从哪查。
// 纯函数、headless 可测；不在本层构造任何 QWidget。

#include "ProductSpec.h"
#include "ToolboxHub.h"
#include "UiSlotHub.h"

#include <QList>
#include <QString>

namespace nextpilot::plotscope::registry
{

// 一条未命中的面板引用诊断。
struct PanelReferenceIssue
{
    QString source;   // 声明来源（provider / activity id / 产品布局 region）
    QString type;     // 未命中的面板类型 id
    QString message;  // 诊断正文：归属 + 可用类型查询入口
};

// 校验全部声明引用；返回空清单 = 全部命中工厂表。
// ToolboxHub 版本为兼容入口；壳侧（A8b-M2 裁定 52441633 §三）走声明清单版本，
// 面板清单经 workbench::IPanelsService::panels() 读回。
QList<PanelReferenceIssue> validatePanelReferences(
    const ToolboxHub &toolbox,
    const UiSlotHub &slotHub,
    const QList<ProductPanelPlacement> &placements);
QList<PanelReferenceIssue> validatePanelReferences(
    const QList<PanelSpec> &panels,
    const UiSlotHub &slotHub,
    const QList<ProductPanelPlacement> &placements);
// 壳侧版本（裁定 52442006 Step 4）：壳不再持 UiSlotHub，Activity 清单经
// ISlotsService::activities() 取回后传入，语义与 UiSlotHub 版完全一致。
QList<PanelReferenceIssue> validatePanelReferences(
    const QList<PanelSpec> &panels,
    const QList<ActivitySpec> &activities,
    const QList<ProductPanelPlacement> &placements);

} // namespace nextpilot::plotscope::registry
