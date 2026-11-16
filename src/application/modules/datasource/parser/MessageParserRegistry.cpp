#include "datasource/MessageParserRegistry.h"

#include <QReadLocker>
#include <QWriteLocker>

#include <algorithm>
#include <utility>

MessageParserRegistry &MessageParserRegistry::instance()
{
    static MessageParserRegistry registry;
    return registry;
}

bool MessageParserRegistry::add(MessageParserSpec spec)
{
    spec.id = spec.id.trimmed();
    spec.protocolId = normalizeProtocol(spec.protocolId);
    spec.displayName = spec.displayName.trimmed();
    if (!spec.isValid())
        return false;

    QWriteLocker lock(&m_lock);
    for (const MessageParserSpec &existing : m_specs) {
        if (existing.id == spec.id)
            return false;
    }
    m_specs.append(std::move(spec));
    std::stable_sort(m_specs.begin(), m_specs.end(),
                     [](const MessageParserSpec &a, const MessageParserSpec &b) {
                         return a.priority > b.priority;
                     });
    return true;
}

bool MessageParserRegistry::remove(const QString &id)
{
    QWriteLocker lock(&m_lock);
    for (int i = 0; i < m_specs.size(); ++i) {
        if (m_specs.at(i).id == id) {
            m_specs.removeAt(i);
            return true;
        }
    }
    return false;
}

void MessageParserRegistry::clear()
{
    QWriteLocker lock(&m_lock);
    m_specs.clear();
}

std::optional<MessageParserSpec> MessageParserRegistry::find(const QString &protocolId) const
{
    const QString key = normalizeProtocol(protocolId);
    QReadLocker lock(&m_lock);
    for (const MessageParserSpec &spec : m_specs) {
        if (spec.protocolId == key)
            return spec;
    }
    return std::nullopt;
}

QVector<MessageParserSpec> MessageParserRegistry::all() const
{
    QReadLocker lock(&m_lock);
    return m_specs;
}

QStringList MessageParserRegistry::protocolIds() const
{
    QReadLocker lock(&m_lock);
    QStringList ids;
    for (const MessageParserSpec &spec : m_specs) {
        if (!ids.contains(spec.protocolId))
            ids.append(spec.protocolId);
    }
    return ids;
}

std::optional<MessageParserSpec> MessageParserRegistry::findByExtension(const QString &suffix) const
{
    const QString key = normalizeSuffix(suffix);
    if (key.isEmpty())
        return std::nullopt;

    QReadLocker lock(&m_lock);
    for (const MessageParserSpec &spec : m_specs) {
        if (matchesExtension(spec.fileExtensions, key))
            return spec;
    }
    return std::nullopt;
}

std::optional<MessageParserSpec> MessageParserRegistry::findBySniff(const QByteArray &data) const
{
    QReadLocker lock(&m_lock);
    for (const MessageParserSpec &spec : m_specs) {
        if (spec.sniff && spec.sniff(data))
            return spec;
        if (!spec.sniff && spec.factory) {
            auto parser = spec.factory();
            if (parser && parser->sniff(data))
                return spec;
        }
    }
    return std::nullopt;
}

std::unique_ptr<IMessageParser> MessageParserRegistry::create(const QString &protocolId) const
{
    auto spec = find(protocolId);
    if (!spec || !spec->factory)
        return nullptr;
    return spec->factory();
}

bool MessageParserRegistry::parse(const QString &protocolId, const QByteArray &data,
                                  PsDecodedFrame &out) const
{
    auto parser = create(protocolId);
    if (!parser)
        return false;
    if (out.protocol.isEmpty())
        out.protocol = normalizeProtocol(protocolId);
    return parser->parse(data, out);
}

QString MessageParserRegistry::normalizeProtocol(const QString &protocolId)
{
    return protocolId.trimmed().toLower();
}

QString MessageParserRegistry::normalizeSuffix(const QString &suffix)
{
    QString key = suffix.trimmed().toLower();
    if (key.startsWith(QStringLiteral("*.")))
        key.remove(0, 1);
    if (key.isEmpty())
        return {};
    if (!key.startsWith(QLatin1Char('.')))
        key.prepend(QLatin1Char('.'));
    return key;
}

bool MessageParserRegistry::matchesExtension(const QStringList &extensions, const QString &suffix)
{
    for (const QString &extension : extensions) {
        if (normalizeSuffix(extension) == suffix)
            return true;
    }
    return false;
}
