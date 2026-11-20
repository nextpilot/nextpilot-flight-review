#include "ScriptFacade.h"

namespace nextpilot::plotscope::context
{


ScriptFacade::ScriptFacade(IIntentExecutor &executor, JobRecorder *recorder,
                           ViewActionDispatcher viewDispatcher)
    : m_executor(executor), m_recorder(recorder), m_viewDispatcher(std::move(viewDispatcher)) {}

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

Result<QVariant> ScriptFacade::callView(const QString &id, const QVariantMap &params)
{
    if (!m_viewDispatcher)
        return fail(QStringLiteral("enoView"), QStringLiteral("view action 派发器不可用（headless？）: %1").arg(id));
    Result<QVariant> out{QVariant()};
    if (!m_viewDispatcher(id, params, out))
        return fail(QStringLiteral("eunsupported"), QStringLiteral("没有视图能处理动作: %1").arg(id));
    return out;
}

Result<QVariant> ScriptFacade::callAny(const QString &op, const QVariantMap &params)
{
    // 1) 先试模块意图通道。
    Result<QVariant> r = call(op, params);
    if (r)
        return r;
    // 2) 模块不支持时回退 view action 通道。
    if (r.error().code == QStringLiteral("eunsupported") && m_viewDispatcher)
        return callView(op, params);
    return r;
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

} // namespace nextpilot::plotscope::context
