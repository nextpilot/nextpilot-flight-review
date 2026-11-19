#pragma once

#include "Result.h"

#include <QJsonObject>
#include <QString>
#include <QVariantMap>

namespace nextpilot::plotscope::core
{

// 意图 = GUI 点击与脚本调用的统一产物。串起来就是可回放的 Job。
struct Intent
{
    QString op;             // 例如 "data.load" / "plot.render"
    QVariantMap params;
};

// 意图执行结果：可带一段数据（如渲染用的 PlotSpec），供上层消费。
struct IntentResult
{
    QVariant payload;
};

// 执行器抽象：core 只定义契约，具体实现在 host。
// 模块通过提供"能力"（能处理某些意图）参与执行。
//
// canHandle 接收完整 Intent 而非只有 op：多个知识包可能声明同一个 op（如
// CSV/ULog 都处理 data.load），必须能按 format 等参数精确认领，才能做到
// 多扩展独立加载、互不抢占、卸载其一不影响其余。
class IIntentHandler
{
public:
    virtual ~IIntentHandler() = default;
    virtual bool canHandle(const Intent &intent) const = 0;
    virtual Result<IntentResult> execute(const Intent &intent) = 0;
};

// Job：一串可序列化的意图，是 GUI 录制与 CLI 执行的公共货币。
class Job
{
public:
    void append(Intent intent);
    int size() const { return m_intents.size(); }
    const Intent &at(int i) const { return m_intents.at(i); }

    QJsonObject toJson() const;
    static Job fromJson(const QJsonObject &obj, Result<void> *outError = nullptr);

private:
    QVector<Intent> m_intents;
};

} // namespace nextpilot::plotscope::core
