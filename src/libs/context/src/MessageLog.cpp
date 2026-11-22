#include "MessageLog.h"

#include <QDateTime>

namespace nextpilot::plotscope::context
{

void MessageLog::append(const Message &message)
{
    m_messages.append(message);
    // 环形缓冲：只丢最旧的，保证最后一个消息一定在（脚本报错不能被后续输出挤出）。
    while (m_messages.size() > kMaxMessages)
        m_messages.removeFirst();
}

void MessageLog::append(const QString &level, const QString &source, const QString &text,
                        const QString &locator)
{
    Message m;
    m.level = level;
    m.source = source;
    m.text = text;
    m.locator = locator;
    m.ts = QDateTime::currentMSecsSinceEpoch();
    append(m);
}

QVector<Message> MessageLog::filtered(const QStringList &levels) const
{
    if (levels.isEmpty())
        return m_messages;
    QVector<Message> out;
    out.reserve(m_messages.size());
    for (const Message &m : m_messages)
        if (levels.contains(m.level))
            out.append(m);
    return out;
}

} // namespace nextpilot::plotscope::context
