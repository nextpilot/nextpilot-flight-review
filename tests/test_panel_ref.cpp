// 验收 4：面板引用启动期校验。
//
// 声明侧两源（A2d 第②步：旧包面板声明面退场——Activity panelTypes、
// centralEditor / 产品默认布局）引用的视图 id 必须在声明面（views / customEditors）
// 命中；未命中必须产出 error 诊断，且诊断说清来源模块、未命中类型、可用类型
// 从哪查。本用例故意声明不存在的视图 id，断言校验器真的报错（不是"我觉得会炸"）。
//
// A2d 第①步：工厂表命中面（旧工厂 types()）随旧工厂类型删除而
// 退场——命中面 = views 声明（knownViewIds，壳经 IViewsService 判定后传入）
// ∪ customEditors 声明（customEditorViewTypes）。
//
// 独立链接：只链 ps_registry + Qt6::Core，headless 不拉 Widgets。

#include "PanelReferenceValidator.h"
#include "ProductSpec.h"
#include "RegistryTree.h"
#include "ToolboxHub.h"
#include "UiSlotHub.h"

#include <QCoreApplication>
#include <QStringList>

#include <cstdio>
#include <utility>

using nextpilot::plotscope::registry::ActivitySpec;
using nextpilot::plotscope::registry::PanelReferenceIssue;
using nextpilot::plotscope::registry::ProductPanelPlacement;
using nextpilot::plotscope::registry::RegistryTree;
using nextpilot::plotscope::registry::UiSlotHub;

namespace {
int g_failed = 0;
#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);                                                        \
            ++g_failed;                                                                                                \
        }                                                                                                              \
    } while (0)

const PanelReferenceIssue *findIssue(const QList<PanelReferenceIssue> &issues, const QString &type)
{
    for (const PanelReferenceIssue &issue : issues)
        if (issue.type == type)
            return &issue;
    return nullptr;
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    RegistryTree tree;
    UiSlotHub slotHub(tree);

    // --- 基线：空声明、空布局 → 零问题 ---
    {
        const QList<ProductPanelPlacement> emptyPlacements;
        const auto issues = validatePanelReferences(slotHub, emptyPlacements);
        CHECK(issues.isEmpty());
    }

    // --- 负向：两处来源各声明一个不存在的视图 id ---
    ActivitySpec activity;
    activity.id = QStringLiteral("test.activity");
    activity.panelTypes = {QStringLiteral("bogus-side-panel")};
    activity.centralEditor = QStringLiteral("bogus-central");
    slotHub.registerActivity(activity);

    QList<ProductPanelPlacement> placements;
    ProductPanelPlacement placement;
    placement.viewId = QStringLiteral("bogus-placement");
    placements.append(placement);

    const auto issues = validatePanelReferences(slotHub, placements);

    // 三条负例各"红"一次：校验器必须逐条报出（可见证据，PR 贴原文）。
    for (const PanelReferenceIssue &issue : issues)
        std::printf("  [panel-ref 负例检出] %s :: %s\n", qPrintable(issue.source), qPrintable(issue.type));

    // 两处来源共三个未命中（activity 两个：panelTypes + centralEditor）。
    CHECK(issues.size() == 3);

    const PanelReferenceIssue *side = findIssue(issues, QStringLiteral("bogus-side-panel"));
    CHECK(side != nullptr);
    if (side)
    {
        CHECK(side->source.contains(QStringLiteral("test.activity")));
        CHECK(side->message.contains(QStringLiteral("bogus-side-panel")));
        CHECK(side->message.contains(QStringLiteral("views 声明")));
    }

    const PanelReferenceIssue *central = findIssue(issues, QStringLiteral("bogus-central"));
    CHECK(central != nullptr);
    if (central)
        CHECK(central->source.contains(QStringLiteral("centralEditor")));

    const PanelReferenceIssue *placementIssue = findIssue(issues, QStringLiteral("bogus-placement"));
    CHECK(placementIssue != nullptr);
    if (placementIssue)
        CHECK(placementIssue->source.contains(QStringLiteral("产品默认布局")));

    // --- 正向：命中 views 声明面的类型不出现在问题清单里 ---
    ActivitySpec hitActivity;
    hitActivity.id = QStringLiteral("hit.activity");
    hitActivity.panelTypes = {QStringLiteral("known-view")};
    slotHub.registerActivity(hitActivity);

    // known-view 由壳经 IViewsService 判定命中后传入（这里模拟已判定的命中面）。
    const QStringList knownViewIds{QStringLiteral("known-view")};
    const auto afterHit = validatePanelReferences(slotHub, placements, knownViewIds, {});
    CHECK(findIssue(afterHit, QStringLiteral("known-view")) == nullptr);
    // 原有未命中依旧如实上报。
    CHECK(afterHit.size() == 3);

    // --- 正向：命中 customEditors 声明面的 centralEditor 不再报问题 ---
    ActivitySpec hitEditor;
    hitEditor.id = QStringLiteral("hit.editor");
    hitEditor.centralEditor = QStringLiteral("known-editor");
    slotHub.registerActivity(hitEditor);
    const auto afterEditor =
        validatePanelReferences(slotHub, placements, knownViewIds, QStringList{QStringLiteral("known-editor")});
    CHECK(findIssue(afterEditor, QStringLiteral("known-editor")) == nullptr);
    CHECK(afterEditor.size() == 3);

    if (g_failed)
    {
        std::printf("panel_ref: %d FAILED\n", g_failed);
        return 1;
    }
    std::printf("panel_ref: ALL PASS\n");
    return 0;
}
