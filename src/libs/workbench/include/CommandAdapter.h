#pragma once

// IIntentHandler → ICommands::Handler 的一次性适配工具（IKXUTD §1.5 / 评论 19:16 落差2）。
//
// 正文明确：不给 ICommands 加 IIntentHandler* 重载，唯一注册面是
// registerCommand(id, Handler)。本文件提供一个 inline 工具函数，调用点一行完成适配：
//     commands.registerCommand(id, asCommandHandler(h, id));
// 它不是第二个注册 API，只是把仓库既有的 IIntentHandler 池桥接到 ICommands 的 Handler 签名。
//
// 桥接口径（按代码现实，非纸面文字）：
//   Intent.op    = id      （正文/评论写 Intent.type，代码实际字段是 op）
//   Intent.params = args   （正文/评论写 Intent.payload，代码实际字段是 params）
//   execute 成功 → Result<QVariant>(IntentResult.payload)；失败 → 透传 Error。

#include "ICommands.h"
#include "Intent.h"

#include <QString>
#include <QVariant>
#include <QVariantMap>

namespace nextpilot::plotscope::workbench {

inline ICommands::Handler asCommandHandler(nextpilot::plotscope::intent::IIntentHandler *h, const QString &id)
{
    return [h, id](const QVariantMap &args) -> Result<QVariant> {
        if (!h)
            return Result<QVariant>(
                nextpilot::plotscope::foundation::fail(QStringLiteral("E_NO_HANDLER"), QStringLiteral("空命令处理器")));
        nextpilot::plotscope::intent::Intent in;
        in.op = id;
        in.params = args;
        auto r = h->execute(in);
        if (!r.ok())
            return Result<QVariant>(r.error());
        return Result<QVariant>(r.value().payload);
    };
}

} // namespace nextpilot::plotscope::workbench
