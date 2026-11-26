#include "StatusHub.h"

#include <QStringList>

#include <algorithm>

namespace nextpilot::plotscope::context
{

StatusBarItem::StatusBarItem(const QString &itemId, StatusHub *hub)
    : QObject(hub), id(itemId), m_hub(hub)
{
}

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
    // 动态条目显示态同步复位（声明表已清）；句柄仍可再 show。
    for (auto *item : m_dynamicItems)
        item->m_visible = false;
}

StatusBarItem *StatusHub::createStatusBarItem(registry::StatusSide alignment, int priority)
{
    // id 自动生成（"dynamic.N"）：调用方不挑 id，避免动态条目撞声明键。
    const auto item = new StatusBarItem(QStringLiteral("dynamic.%1").arg(++m_dynamicCounter), this);
    item->alignment = alignment;
    item->priority = priority;
    m_dynamicItems.append(item);
    return item;
}

void StatusHub::unregisterSpec(const QString &id)
{
    if (!m_specs.contains(id))
        return;
    m_specs.remove(id);
    m_text.remove(id);
}

// ---- StatusBarItem 行为（vscode.StatusBarItem 对位，语义见头注释）----

void StatusBarItem::show()
{
    if (m_disposed || m_visible || !m_hub)
        return;
    registry::StatusSpec spec;
    spec.id = id;
    spec.text = text;
    spec.tooltip = tooltip;
    spec.side = alignment;
    spec.priority = priority;
    if (!command.isEmpty())
        spec.commandId = command;
    m_hub->registerSpec(spec); // 声明 + statusSpecDeclared + 初始文案入表
    m_visible = true;
}

void StatusBarItem::hide()
{
    if (m_disposed || !m_visible || !m_hub)
        return;
    m_hub->unregisterSpec(id);
    m_visible = false;
}

void StatusBarItem::dispose()
{
    if (m_disposed)
        return; // 幂等
    if (m_visible)
        hide();
    m_disposed = true;
    deleteLater(); // hub 不再留句柄；QObject 父（hub）保证析构顺序
}

void StatusBarItem::setText(const QString &t)
{
    text = t;
    if (m_visible && m_hub)
        m_hub->updateStatus(id, text);
}

void StatusBarItem::setTooltip(const QString &tip)
{
    tooltip = tip; // 声明期字段：show 后变更需 hide/show 生效（headless 无渲染面）
}

void StatusBarItem::setCommand(const QString &cmd)
{
    command = cmd; // 同上
}

} // namespace nextpilot::plotscope::context
