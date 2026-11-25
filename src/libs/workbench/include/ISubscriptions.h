#pragma once

// A8b-M2 裁定 52441074 §2：变化通知统一走 ISubscriptions（VSCode context.subscriptions 对位）。
// M1 的 e0 三接口（ExtensionContext/IExtensionService/ISubscriptions）当时顺延未落地——
// 本项补建最小可用形态：一组可撤销订阅 + 整批 dispose，供动作提供器注册监听、卸载时一次性退订。
// 纯契约 + 头文件内联实现（ps_workbench 是 INTERFACE 库，零编译单元）。

#include "Subscription.h"

#include <QObject> // QObject::disconnect

#include <QVector>

namespace nextpilot::plotscope::workbench
{

class ISubscriptions
{
public:
    virtual ~ISubscriptions() = default;

    // 登记一个可撤销订阅；空句柄忽略。
    virtual void add(Subscription sub) = 0;
    virtual int count() const = 0;
    // 全部退订并清空（owner 卸载时调用）。
    virtual void disposeAll() = 0;
};

// 默认实现：Qt 直连语义，disposeAll 后句柄失效。
class SubscriptionBag final : public ISubscriptions
{
public:
    ~SubscriptionBag() override { disposeAll(); }
    void add(Subscription sub) override
    {
        if (sub)
            m_items.append(sub);
    }
    int count() const override { return m_items.size(); }
    void disposeAll() override
    {
        for (const Subscription &s : m_items)
            QObject::disconnect(s);
        m_items.clear();
    }

private:
    QVector<Subscription> m_items;
};

} // namespace nextpilot::plotscope::workbench
