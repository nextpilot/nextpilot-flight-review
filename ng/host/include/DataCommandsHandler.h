#pragma once

#include "AppContext.h"
#include "IIntentExecutor.h"
#include "Intent.h"
#include "OpDispatch.h"

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
//   · data.series      —— 读一条通道的 t/v 数组进 payload（脚本/CLI/MCP 的数据出口，
//                          VBA 能力基石：脚本内可取数做统计判断）。dataset 缺省取唯一
//                          数据集，table 缺省取唯一表，limit 缺省 4096、上限 200000。
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
        : m_app(appCtx), m_executor(executor)
    {
        buildDispatch();
    }

    bool canHandle(const nextpilot::plotscope::intent::Intent &intent) const override;
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> execute(
        const nextpilot::plotscope::intent::Intent &intent) override;

    // 最近文件在 settings 中的键（data.load 与 data.recent 共用）。
    static constexpr const char *kRecentKey = "data/recent";

private:
    // op → 成员函数注册表（机制唯一：OpDispatch，op 字符串只写一遍）。
    void buildDispatch();

    // 兜底解析（提前备好的资产，全 handler 共用，不再逐方法手抄）：
    // dataset 缺省取唯一数据集 + 存在性校验。
    nextpilot::plotscope::foundation::Result<int> resolveDataset(
        const nextpilot::plotscope::intent::Intent &intent) const;
    // table 缺省取唯一表 + 存在性校验。
    nextpilot::plotscope::foundation::Result<QString> resolveTable(
        const nextpilot::plotscope::dataset::Dataset &ds, const QString &table) const;

    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> reload(const nextpilot::plotscope::intent::Intent &intent);
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> exportCsv(const nextpilot::plotscope::intent::Intent &intent);
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> recent(const nextpilot::plotscope::intent::Intent &intent);
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> clearCache();
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> removeSource(const nextpilot::plotscope::intent::Intent &intent);
    // live 源暂停/继续（#193）：切 SourceState::Streaming/Paused。
    // 仅 SourceKind::Live 源可操作；文件源返回 enotsupported。幂等：已在目标状态直接成功。
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> setSourcePaused(
        const nextpilot::plotscope::intent::Intent &intent, bool paused);
    // 源级时间对齐（T3.2）：mode = base/start/end/center/reset。
    // 只改 Dataset 出口偏移（见 Dataset::setTimeOffsetUs），原始时间戳不动。
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> alignTime(
        const nextpilot::plotscope::intent::Intent &intent);
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> derive(const nextpilot::plotscope::intent::Intent &intent);
    // MIMO 派生：四元数 → roll/pitch/yaw（独立派生字段，不写回 dataset）。
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> deriveMimoQuat(const nextpilot::plotscope::intent::Intent &intent);
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> filterSeries(const nextpilot::plotscope::intent::Intent &intent);
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> runMarker(const nextpilot::plotscope::intent::Intent &intent);
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> stateSeries(const nextpilot::plotscope::intent::Intent &intent);
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> series(const nextpilot::plotscope::intent::Intent &intent);
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> workspaceList();
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> workspaceSave();
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> workspaceRestore();
    // dataset 级通道变换配方（变换 = 非破坏视图，全后端生效；plot.channel / data.series 消费）。
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> setChannelTransform(const nextpilot::plotscope::intent::Intent &intent);
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> clearChannelTransform(const nextpilot::plotscope::intent::Intent &intent);
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> deriveList(const nextpilot::plotscope::intent::Intent &intent);
    // MIMO 派生流式增量刷新：对该 dataset 全部 quat_to_rpy 派生按注册表配方指纹
    // 重建 intent 逐组重发（同名重发 = 增量，见 deriveMimoQuat）。
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> refreshDerives(const nextpilot::plotscope::intent::Intent &intent);

    nextpilot::plotscope::context::AppContext &m_app;
    nextpilot::plotscope::intent::IIntentExecutor &m_executor;
    nextpilot::plotscope::intent::OpDispatch m_dispatch;
};
