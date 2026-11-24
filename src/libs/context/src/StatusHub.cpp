#include "StatusHub.h"

#include <QStringList>

#include <algorithm>

namespace nextpilot::plotscope::context
{

StatusHub::StatusHub(QObject *parent) : QObject(parent) {}

void StatusHub::registerSpec(const registry::StatusSpec &spec)
{
    if (spec.id.isEmpty())
        return;
    if (m_specs.contains(spec.id))
        return; // 先声明者优先

    registry::StatusSpec s = spec;
    if (s.provider.isEmpty())
        s.provider = QStringLiteral("module");
    m_specs.insert(s.id, s);
    // 声明里给的 text 只作**初始文案**；此后运行态文案归本类，不再回写 spec。
    m_text.insert(s.id, s.text);
    Q_EMIT statusSpecDeclared(s);
}

QList<registry::StatusSpec> StatusHub::specs() const
{
    QList<registry::StatusSpec> all;
    all.reserve(m_specs.size());
    for (auto it = m_specs.constBegin(); it != m_specs.constEnd(); ++it)
        all.append(it.value());
    // 同侧小 priority 靠前；同权重按 id 字典序，保证装配顺序不影响视觉。
    std::sort(all.begin(), all.end(), [](const registry::StatusSpec &a, const registry::StatusSpec &b) {
        if (a.side != b.side)
            return static_cast<int>(a.side) < static_cast<int>(b.side);
        if (a.priority != b.priority)
            return a.priority < b.priority;
        return a.id < b.id;
    });
    return all;
}

QList<registry::StatusSpec> StatusHub::specsOf(registry::StatusSide side) const
{
    QList<registry::StatusSpec> out;
    for (const registry::StatusSpec &s : specs())
        if (s.side == side)
            out.append(s);
    return out;
}

const registry::StatusSpec *StatusHub::spec(const QString &id) const
{
    const auto it = m_specs.constFind(id);
    return it == m_specs.constEnd() ? nullptr : &it.value();
}

void StatusHub::updateStatus(const QString &id, const QString &text)
{
    const auto sit = m_specs.constFind(id);
    if (sit == m_specs.constEnd())
        return; // 未声明忽略
    if (m_text.value(id) == text)
        return; // 未变不刷新
    m_text.insert(id, text);
    Q_EMIT statusChanged(id, text);
}

QString StatusHub::text(const QString &id) const
{
    return m_text.value(id);
}

void StatusHub::flash(const QString &text, int durationMs)
{
    if (text.isEmpty() || durationMs <= 0)
        return;
    Q_EMIT statusFlashed(text); // 时长交给壳（QStatusBar 的 timeout 语义）
}

void StatusHub::clear()
{
    m_specs.clear();
    m_text.clear();
}

} // namespace nextpilot::plotscope::context
