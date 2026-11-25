// 验收 4：面板引用启动期校验。
//
// 声明侧三处（包 PanelSpec / Activity panelTypes、centralPanel / 产品默认布局）
// 引用的面板类型必须在工厂表命中；未命中必须产出 error 诊断，且诊断说清
// 来源模块、未命中类型、可用类型从哪查。本用例故意声明不存在的面板类型，
// 断言校验器真的报错（不是"我觉得会炸"）。
//
// 独立链接：只链 ps_registry + Qt6::Core，headless 不拉 Widgets。

#include "PanelFactoryHub.h"
#include "PanelReferenceValidator.h"
#include "ProductSpec.h"
#include "RegistryTree.h"
#include "Region.h"
#include "ToolboxHub.h"
#include "UiSlotHub.h"

#include <QCoreApplication>
#include <QStringList>

#include <cstdio>
#include <utility>

class QWidget;

using nextpilot::plotscope::context::AppContext;
using nextpilot::plotscope::intent::IIntentExecutor;
using nextpilot::plotscope::registry::ActivitySpec;
using nextpilot::plotscope::registry::PanelFactoryHub;
using nextpilot::plotscope::registry::PanelReferenceIssue;
using nextpilot::plotscope::registry::PanelSpec;
using nextpilot::plotscope::registry::Pack;
using nextpilot::plotscope::registry::ProductPanelPlacement;
using nextpilot::plotscope::registry::ProductSpec;
using nextpilot::plotscope::registry::RegistryTree;
using nextpilot::plotscope::registry::Region;
using nextpilot::plotscope::registry::ToolboxHub;
using nextpilot::plotscope::registry::UiSlotHub;

namespace
{
int g_failed = 0;
#define CHECK(cond)                                                            \
    do                                                                         \
    {                                                                          \
        if (!(cond))                                                           \
        {                                                                      \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);                \
            ++g_failed;                                                        \
        }                                                                      \
    } while (0)

// 工厂只占位：签名与真实工厂一致，构造体返回 nullptr（headless 不碰 QWidget 实例）。
QWidget *dummyFactory(AppContext &, IIntentExecutor &, QWidget *)
{
    return nullptr;
}

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
    PanelFactoryHub::reset(); // 测试间隔离静态工厂表

    RegistryTree tree;
    ToolboxHub toolbox(tree);
    UiSlotHub slotHub(tree);

    // 命中的工厂类型：后续正向用例声明它，校验器不应报问题。
    PanelFactoryHub::registerPanel(QStringLiteral("known-panel"), &dummyFactory,
                                   QStringLiteral("已知面板"));

    // --- 基线：空声明、空布局 → 零问题 ---
    {
        const QList<ProductPanelPlacement> emptyPlacements;
        const auto issues = validatePanelReferences(toolbox, slotHub, emptyPlacements);
        CHECK(issues.isEmpty());
    }

    // --- 负向：三处来源各声明一个不存在的面板类型 ---
    ActivitySpec activity;
    activity.id = QStringLiteral("test.activity");
    activity.panelTypes = {QStringLiteral("bogus-side-panel")};
    activity.centralPanel = QStringLiteral("bogus-central");
    slotHub.registerActivity(activity);

    Pack pack;
    pack.id = QStringLiteral("pack.test");
    PanelSpec packPanel;
    packPanel.id = QStringLiteral("bogus-pack-panel");
    pack.panels.append(packPanel);
    CHECK(toolbox.mount(std::move(pack)));

    QList<ProductPanelPlacement> placements;
    ProductPanelPlacement placement;
    placement.region = Region::Panel;
    placement.panelType = QStringLiteral("bogus-placement");
    placements.append(placement);

    const auto issues = validatePanelReferences(toolbox, slotHub, placements);

    // 三条来源共四个未命中（activity 两个：panelTypes + centralPanel）。
    CHECK(issues.size() == 4);

    const PanelReferenceIssue *side = findIssue(issues, QStringLiteral("bogus-side-panel"));
    CHECK(side != nullptr);
    if (side)
    {
        CHECK(side->source.contains(QStringLiteral("test.activity")));
        CHECK(side->message.contains(QStringLiteral("bogus-side-panel")));
        CHECK(side->message.contains(QStringLiteral("PanelFactoryHub::types()")));
    }

    const PanelReferenceIssue *central = findIssue(issues, QStringLiteral("bogus-central"));
    CHECK(central != nullptr);
    if (central)
        CHECK(central->source.contains(QStringLiteral("centralPanel")));

    const PanelReferenceIssue *packIssue = findIssue(issues, QStringLiteral("bogus-pack-panel"));
    CHECK(packIssue != nullptr);
    if (packIssue)
    {
        CHECK(packIssue->source.contains(QStringLiteral("pack.test")));
        CHECK(packIssue->message.contains(QStringLiteral("PS_REGISTER_PANEL")));
    }

    const PanelReferenceIssue *placementIssue = findIssue(issues, QStringLiteral("bogus-placement"));
    CHECK(placementIssue != nullptr);
    if (placementIssue)
        CHECK(placementIssue->source.contains(QStringLiteral("产品默认布局")));

    // --- 正向：命中工厂表的类型不出现在问题清单里 ---
    ActivitySpec hitActivity;
    hitActivity.id = QStringLiteral("hit.activity");
    hitActivity.panelTypes = {QStringLiteral("known-panel")};
    slotHub.registerActivity(hitActivity);

    const auto afterHit = validatePanelReferences(toolbox, slotHub, placements);
    CHECK(findIssue(afterHit, QStringLiteral("known-panel")) == nullptr);
    // 原有未命中依旧如实上报。
    CHECK(afterHit.size() == 4);

    if (g_failed)
    {
        std::printf("panel_ref: %d FAILED\n", g_failed);
        return 1;
    }
    std::printf("panel_ref: ALL PASS\n");
    return 0;
}
