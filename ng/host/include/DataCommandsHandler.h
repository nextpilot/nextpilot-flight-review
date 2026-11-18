#pragma once

#include "Intent.h"

namespace ng::core
{
class AppContext;
class IIntentExecutor;
} // namespace ng::core

// 数据器官的"派生能力"：reload / export / recent / clearCache。
//
// 与 DataLoadHandler（系统级 data.load 入口）归口一致，都是宿主级 handler——
// 因为它们都要访问宿主持有的 AppContext（repo / sources / settings），
// 不归属任何可卸载模块。这样所有 data.* 意图集中一处，不建第二条路。
//
// 能力来源：参考老代码 src/application/modules/datasource/DataSourceCommands.cpp，
// 但只实现 ng 底层有支撑的那些——不造点了没反应的空按钮：
//   · data.reload      —— 活动源重新走 data.load（有 sources + load 意图）
//   · data.export      —— 活动通道经 Dataset::channel 取数写 CSV（有 readWindow）
//   · data.recent      —— 最近文件路径，存 settings（有 SettingsStore）
//   · data.clearCache  —— 删除各源的 .psc 解析缓存（有缓存路径约定）
//
// 刻意不搬：protocol.inspect（领域面板，归 extensions/<域>/gui/）、
// layout.save/import（已有 plot.layout.*）、file.open（已被 data.load 统一）。
class DataCommandsHandler final : public ng::core::IIntentHandler
{
public:
    explicit DataCommandsHandler(ng::core::AppContext &appCtx,
                                 ng::core::IIntentExecutor &executor)
        : m_app(appCtx), m_executor(executor) {}

    bool canHandle(const ng::core::Intent &intent) const override;
    ng::core::Result<ng::core::IntentResult> execute(
        const ng::core::Intent &intent) override;

    // 最近文件在 settings 中的键（data.load 与 data.recent 共用）。
    static constexpr const char *kRecentKey = "data/recent";

private:
    ng::core::Result<ng::core::IntentResult> reload(const ng::core::Intent &intent);
    ng::core::Result<ng::core::IntentResult> exportCsv(const ng::core::Intent &intent);
    ng::core::Result<ng::core::IntentResult> recent(const ng::core::Intent &intent);
    ng::core::Result<ng::core::IntentResult> clearCache();

    ng::core::AppContext &m_app;
    ng::core::IIntentExecutor &m_executor;
};
