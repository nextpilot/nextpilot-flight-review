#pragma once

#include "Result.h"

#include <QJsonObject>
#include <QString>
#include <QVariantMap>

namespace nextpilot::plotscope::intent
{

using nextpilot::plotscope::foundation::Result;
using nextpilot::plotscope::foundation::fail;

// 意图：GUI 点击与脚本调用的统一产物。
struct Intent
{
    QString op;             // 例如 "data.load" / "plot.render"
    QVariantMap params;
};

// 执行结果，可携带数据供上层消费。
struct IntentResult
{
    QVariant payload;
};

// 意图处理器。canHandle 收完整 Intent，便于按参数认领同 op 的多个实现。
class IIntentHandler
{
public:
    virtual ~IIntentHandler() = default;
    virtual bool canHandle(const Intent &intent) const = 0;
    virtual Result<IntentResult> execute(const Intent &intent) = 0;
};

// 可序列化的意图序列，用于录制回放。
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

} // namespace nextpilot::plotscope::intent
// B7 过渡别名：core 消亡前全仓仍以 core::Xxx 取用。簇 8 删 core 时移除。
namespace nextpilot::plotscope::core
{
using intent::Intent;
using intent::IntentResult;
using intent::IIntentHandler;
using intent::Job;
} // namespace nextpilot::plotscope::core

