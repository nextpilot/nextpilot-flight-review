#pragma once

// A8b-M2 裁定 52441074 §1/§3：模块 UI 声明从"过程式调 IUiContext::declare*/addAction"
// 反转为"声明式 contributes() 数据"。本文件定义这份数据的无损承载结构。
//
// 字段直接复用 registry 既有纯数据类型（UiAction/ActivitySpec/ThemeTokenSpec/StatusSpec），
// 不丢 UiAction 的 data/visual/shortcut/large/checkable/galleryGroup/intentOp 等呈现字段
// （裁定 §1：checkable 只存布尔，勾选态归 CommandRegistry；这里 only 存声明布尔）。
// 数据无指针、headless 可查；owner = 模块 id，isBuiltin = true（裁定 §4）。
//
// RuntimeHost::activate 收集所有模块的 contributes()，统一灌回 UiSlotHub/StatusHub/theme，
// 壳不再被模块直接驱动（消掉第二声明通道 addAction）。

#include "Contribution.h"
#include "ContributionParser.h" // ContributedViewContainer / ContributedView（M2 views 键）
#include "IActionProvider.h"

#include <QList>
#include <QString>

namespace nextpilot::plotscope::registry
{

// Ribbon 页签声明（原 Ribbon 页声明）。
struct RibbonPageDecl
{
    QString id;
    QString label;
    int order = 0;
};

// Ribbon 分组声明（原 Ribbon 组声明）。
struct RibbonGroupDecl
{
    QString id;
    QString label;
    QString pageId;
    int order = 0;
};

// 动态动作提供器声明（裁定 §2）：slotId -> provider 对象指针（借用）。
// 变化经 provider->onDidChangeActions 通知，装配层订阅后只重建该 slot。
struct ActionProviderDecl
{
    QString owner;
    QString slotId;
    IActionProvider *provider = nullptr; // 借用，生命周期归模块
};

// 一个模块的完整 UI 声明（纯数据）。
struct ModuleContributions
{
    QString owner;      // 模块 id（plot/data/script/analysis/protocol/report/...）
    bool isBuiltin = true;

    QList<RibbonPageDecl> ribbonPages;
    QList<RibbonGroupDecl> ribbonGroups;
    QList<UiAction> actions;                 // 原 addAction（无损，含 gallery/visual/data 等）
    QList<ActivitySpec> activities;          // 原 Activity 声明
    QList<ThemeTokenSpec> themeTokens;       // 原主题 token 声明
    QList<StatusSpec> statuses;              // 原状态栏声明
    QList<ActionProviderDecl> actionProviders; // 原 bindActionProvider（换成对象接口）

    // contributes.viewsContainers / views（M2 视图键，裁定 52442006 Step 5）：
    // 纯数据；宿主收集后经 ContributionsInstaller 灌 ViewRegistry，
    // 壳经 IViewsService 读回。内置模块本期零声明（扩展包经 manifest 走同一张表）。
    QList<ContributedViewContainer> viewContainers;
    QList<ContributedView> views;

    bool isEmpty() const
    {
        return ribbonPages.isEmpty() && ribbonGroups.isEmpty() && actions.isEmpty() &&
               activities.isEmpty() && themeTokens.isEmpty() && statuses.isEmpty() &&
               actionProviders.isEmpty() && viewContainers.isEmpty() && views.isEmpty();
    }
};

} // namespace nextpilot::plotscope::registry
