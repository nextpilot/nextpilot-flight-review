#pragma once

#include "IIntentExecutor.h"
#include "Intent.h" // IIntentHandler 定义在 Intent.h（与 Intent/IntentResult 同头）
#include "Result.h"

#include <QPointer>
#include <atomic>
#include <functional>

namespace nextpilot::plotscope::context
{
class InteractionRegistry;
}

namespace nextpilot::plotscope::dataset
{
class DatasetRegistry;
class SourceRegistry;
}

namespace nextpilot::plotscope::script
{

using nextpilot::plotscope::foundation::Result;
using nextpilot::plotscope::intent::IIntentExecutor;
using nextpilot::plotscope::intent::Intent;
using nextpilot::plotscope::intent::IntentResult;

// ============================================================================
// data.batch.run：目录级批量执行（M2.4）。
//
// 100 个 ULog 一键跑完出汇总：每文件走**同一条真实执行路径**
//   data.load（按扩展名选格式）→ script（独立 VM，batch 全局注入文件上下文，
//   脚本 return 表 = 该文件的关键指标），失败记 {file, stage, error} 明细。
//
// 顺序执行 + 事件循环分片：每文件处理完 processEvents 一次——
// 进度 UI 可刷新（data.batch.progress view action）、取消请求可插入
// （data.batch.cancel 置标志）。不做线程池并行（共享执行器状态，一期不值）。
//
// 结果 payload（QVariantMap，headless JSON 与 GUI 汇总表双出口共用一份）：
//   { dir, script, total, ok, failed, cancelled,
//     rows: [ {file, ok, stage("load"/"script"), error, metrics:{...}} ] }
// ============================================================================
class BatchHandler final : public QObject, public nextpilot::plotscope::intent::IIntentHandler
{
    Q_OBJECT
public:
    // view action 派发器：每文件脚本 app.view 能触达 GUI 能力（与 script.run 同语义）。
    using ViewDispatcher = std::function<bool(const QString &, const QVariantMap &, Result<QVariant> &)>;

    // data 为空指针时跳过"空解析"判定（极端 headless 兜底，正常装配恒有）。
    BatchHandler(IIntentExecutor &executor, context::InteractionRegistry *inter,
                 dataset::DatasetRegistry *data, dataset::SourceRegistry *sources,
                 ViewDispatcher viewDispatcher, QObject *parent = nullptr);

    bool canHandle(const Intent &intent) const override
    {
        return intent.op == QStringLiteral("data.batch.run");
    }
    Result<IntentResult> execute(const Intent &intent) override;

    // data.batch.cancel：协作式取消——下一个分片边界生效，不中断当前文件。
    void requestCancel() { m_cancel.store(true); }
    bool running() const { return m_running.load(); }

private:
    IIntentExecutor &m_executor;
    context::InteractionRegistry *m_inter = nullptr; // 进度上报（headless 为空：静默无 UI）
    dataset::DatasetRegistry *m_data = nullptr;      // 空解析判定（损坏文件 → load 阶段失败）
    dataset::SourceRegistry *m_sources = nullptr;    // 数据集 → 源反查（data.remove 收的是源 id）
    ViewDispatcher m_viewDispatcher;
    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_running{false}; // 重入保护：GUI/脚本并发批量明确报错
};

} // namespace nextpilot::plotscope::script
