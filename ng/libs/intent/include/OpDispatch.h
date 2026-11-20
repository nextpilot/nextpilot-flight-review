#pragma once

#include "Intent.h"
#include "Registry.h"
#include "ErrorCodes.h"

#include <QLatin1String>
#include <QList>
#include <QString>

#include <functional>

namespace nextpilot::plotscope::intent
{

using nextpilot::plotscope::foundation::Result;
using nextpilot::plotscope::foundation::fail;

// op → 执行函数的注册表。意图分发的唯一机制：
//   canHandle = contains(op)；execute = 查表调用；op 字符串只写一遍。
//
// 纪律：所有 IIntentHandler / IModule::Handler 的 op 分发一律用它——
// 禁止手写 canHandle 长链 + execute if-else（曾出现同一批 op 写两遍、
// 两处人工保持同步的形态，收敛于本表）。
class OpDispatch
{
public:
    using Fn = std::function<Result<IntentResult>(const Intent &)>;

    // 登记一个 op 的处理函数。重复登记视为 bug（先到者为准并返回 false）。
    bool reg(const QString &op, Fn fn)
    {
        return m_table.insert(op, std::move(fn));
    }

    // 语法糖：绑定成员函数（绝大多数 handler 的形态）。
    template <typename T>
    bool reg(const QString &op, T *obj, Result<IntentResult> (T::*fn)(const Intent &))
    {
        return m_table.insert(op, [obj, fn](const Intent &i) { return (obj->*fn)(i); });
    }

    bool canHandle(const Intent &intent) const { return m_table.contains(intent.op); }

    Result<IntentResult> execute(const Intent &intent)
    {
        const auto it = m_table.find(intent.op);
        if (!it)
            return fail(nextpilot::plotscope::foundation::err::Intent,
                        QStringLiteral("未注册的意图: %1").arg(intent.op));
        return (*it)(intent);
    }

    // 已登记的 op（调试/自描述用，如 ai.tools.list）。
    QList<QString> ops() const { return m_table.keys(); }

private:
    // 容器本体委托泛型注册表（机制唯一）。
    nextpilot::plotscope::registry::Registry<Fn> m_table;
};

} // namespace nextpilot::plotscope::intent
