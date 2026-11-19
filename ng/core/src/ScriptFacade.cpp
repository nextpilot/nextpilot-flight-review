#include "ScriptFacade.h"

namespace nextpilot::plotscope::core
{

ScriptFacade::ScriptFacade(IIntentExecutor &executor, JobRecorder *recorder)
    : m_executor(executor), m_recorder(recorder) {}

Result<QVariant> ScriptFacade::call(const QString &op, const QVariantMap &params)
{
    Intent intent;
    intent.op = op;
    intent.params = params;
    Result<IntentResult> r = m_executor.execute(intent);
    if (!r)
        return fail(r.error().code, r.error().message);
    // 只录制成功执行的业务意图——回放时得到干净的意图序列。
    if (m_recorder && m_recorder->isRecording())
        m_recorder->record(intent);
    return r.value().payload;
}

Result<DatasetId> ScriptFacade::loadDataset(const QString &path, const QString &format,
                                            std::optional<DatasetId> stableId)
{
    QVariantMap params;
    params[QStringLiteral("path")] = path;
    params[QStringLiteral("format")] = format;
    if (stableId.has_value())
        params[QStringLiteral("id")] = *stableId;
    Result<QVariant> r = call(QStringLiteral("data.load"), params);
    if (!r)
        return fail(r.error().code, r.error().message);
    return r.value().toInt();
}

Result<QVariantMap> ScriptFacade::renderPlot(DatasetId id, const QString &channel)
{
    QVariantMap params;
    params[QStringLiteral("dataset")] = id;
    if (!channel.isEmpty())
        params[QStringLiteral("channel")] = channel;
    Result<QVariant> r = call(QStringLiteral("plot.render"), params);
    if (!r)
        return fail(r.error().code, r.error().message);
    return r.value().toMap();
}

} // namespace nextpilot::plotscope::core
