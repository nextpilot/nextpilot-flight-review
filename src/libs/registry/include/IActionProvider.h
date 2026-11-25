#pragma once

// A8b-M2 裁定 52441074 §2：动态动作提供器（VSCode TreeDataProvider 对位）。
// ribbon 图库是外壳 chrome，不是 view 内容——不能用 registerViewProvider(QWidget*) 承接，
// 也不许把 lambda 交进壳（"壳懂业务"的另一种形态）。正确形态是同一类注册、换承载对象：
// provider 只回答"这个 slot 现在有哪些条目"（纯数据 UiAction，headless 可查），
// 内容变化经 onDidChangeActions 通知，壳订阅后只重建该 slot，不再有全局 refreshActions()。

#include "Contribution.h"

#include <QMetaObject> // QMetaObject::Connection

#include <functional>

namespace nextpilot::plotscope::registry
{

class IActionProvider
{
public:
    virtual ~IActionProvider() = default;

    // 返回该 slot 当前条目（纯数据，无指针）。slotId 即原 UiAction::slotId。
    virtual QList<UiAction> actionsFor(const QString &slotId) const = 0;

    // 内容变化事件（VSCode Event 模式）：回调参数是发生变化的 slotId，
    // 壳据此只重建该 slot。返回可撤销句柄，交给 ISubscriptions 统一管理。
    virtual QMetaObject::Connection
    onDidChangeActions(std::function<void(const QString &slotId)> listener) = 0;
};

} // namespace nextpilot::plotscope::registry
