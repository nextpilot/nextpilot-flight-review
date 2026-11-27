#pragma once

// 面板引用启动期校验（验收 4）。
//
// 声明侧引用视图 id 的入口（A2d 第②步后两源：旧包面板声明面已退场）：
//   1) ActivitySpec::panelTypes / centralPanel（Activity Bar 入口关联的视图）；
//   2) ProductSpec::defaultPanels[].viewId（产品默认布局占位）。
//
// 这些 id 必须在声明面（views 声明 / customEditors 声明）命中；
// 任一未命中即产出一条 error 诊断：说明归属来源、未命中类型、可用类型从哪查。
// 纯函数、headless 可测；不在本层构造任何 QWidget。

#include "ProductSpec.h"
#include "UiSlotHub.h"

#include <QList>
#include <QString>

namespace nextpilot::plotscope::registry
{

// 一条未命中的面板引用诊断。
struct PanelReferenceIssue
{
    QString source;   // 声明来源（activity id / 产品布局）
    QString type;     // 未命中的视图 id
    QString message;  // 诊断正文：归属 + 可用类型查询入口
};

// 校验全部声明引用；返回空清单 = 全部命中声明面。
// knownViewIds：views 声明命中面（A2b 笔2b 第②步）——已迁 ViewRegistry 的 id
// 由壳经 IViewsService 判定后传入；A2d 第①步工厂表命中面随旧工厂类型
// 删除而退场（A2c 收工时表已恒空）。
// customEditorViewTypes：editor 命中面（第③步顺延必修补齐）——customEditors
// 声明的 viewType（IEditorService::customEditors() 取回）由壳传入。
// 命中面 = views ∪ customEditors。
QList<PanelReferenceIssue> validatePanelReferences(const QList<ActivitySpec> &activities,
                                                   const QList<ProductPanelPlacement> &placements,
                                                   const QStringList &knownViewIds = {},
                                                   const QStringList &customEditorViewTypes = {});
// 壳侧版本（裁定 52442006 Step 4）：壳不再持 UiSlotHub，Activity 清单经
// ISlotsService::activities() 取回后传入，语义与 UiSlotHub 版完全一致。
QList<PanelReferenceIssue> validatePanelReferences(const UiSlotHub &slotHub,
                                                   const QList<ProductPanelPlacement> &placements,
                                                   const QStringList &knownViewIds = {},
                                                   const QStringList &customEditorViewTypes = {});

} // namespace nextpilot::plotscope::registry
