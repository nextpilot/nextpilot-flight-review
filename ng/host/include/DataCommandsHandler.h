#pragma once

#include "AppContext.h"
#include "IIntentExecutor.h"
#include "Intent.h"

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
//   · data.remove      —— 移除一个数据源及其产出的全部数据集（有 sources.remove + data.remove）
//   · data.derive      —— Rust 实时算出派生序列交给 UI（ps_preview_filter，只读），
//                          **不写回数据集、不新增通道**：payload 带 time/value 供脚本/CLI，
//                          并发 AppContext::derivedSeriesReady 供画布叠加显示。
//   · data.filter      —— 有状态 Lua 过滤器作用到一条通道（script 模块的 FilterOps），
//                          与 derive 同款只读叠加：不写回数据集，结果走 derivedSeriesReady。
//                          参数 dataset/table/column/filter(id)/params/cap/name。
//   · data.marker      —— Marker 规则（script 模块的 MarkerOps）扫一条通道产出 PlotMarker，
//                          经 plot.markers.set view action 画到活动画布；不写回数据集。
//                          参数 dataset/table/column/code(Luau 规则)/name。
//   · data.state       —— 状态视图（对齐 PJ4 StateTransitionsView）：把一条 bool/整型
//                          （或仅含整数值的浮点）通道游程编码成状态段，经 plot.state.set
//                          view action 画到活动画布顶部的状态色带。标签格式化对齐
//                          StateSeriesAdapter：bool→"true"/"false"、整数→十进制串、
//                          浮点非整数值断段（gap）。参数 dataset/table/column/name。
//   · data.workspace.save/list/restore —— 工作区数据源配置的持久化与恢复。
//                          恢复 = 重放 data.load + data.derive（配方），不存数据本体。
//
// 刻意不搬：protocol.inspect（领域面板，归 extensions/<域>/gui/）、
// layout.save/import（已有 plot.layout.*）、file.open（已被 data.load 统一）。
class DataCommandsHandler final : public nextpilot::plotscope::intent::IIntentHandler
{
public:
    explicit DataCommandsHandler(nextpilot::plotscope::context::AppContext &appCtx,
                                 nextpilot::plotscope::intent::IIntentExecutor &executor)
        : m_app(appCtx), m_executor(executor) {}

    bool canHandle(const nextpilot::plotscope::intent::Intent &intent) const override;
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> execute(
        const nextpilot::plotscope::intent::Intent &intent) override;

    // 最近文件在 settings 中的键（data.load 与 data.recent 共用）。
    static constexpr const char *kRecentKey = "data/recent";

private:
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> reload(const nextpilot::plotscope::intent::Intent &intent);
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> exportCsv(const nextpilot::plotscope::intent::Intent &intent);
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> recent(const nextpilot::plotscope::intent::Intent &intent);
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> clearCache();
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> removeSource(const nextpilot::plotscope::intent::Intent &intent);
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> derive(const nextpilot::plotscope::intent::Intent &intent);
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> filterSeries(const nextpilot::plotscope::intent::Intent &intent);
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> runMarker(const nextpilot::plotscope::intent::Intent &intent);
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> stateSeries(const nextpilot::plotscope::intent::Intent &intent);
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> workspaceList();
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> workspaceSave();
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> workspaceRestore();

    nextpilot::plotscope::context::AppContext &m_app;
    nextpilot::plotscope::intent::IIntentExecutor &m_executor;
};
