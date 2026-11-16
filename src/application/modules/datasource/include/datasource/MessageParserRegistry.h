#pragma once

#include "MessageParserSpec.h"

#include <QByteArray>
#include <QReadWriteLock>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>
#include <optional>

class MessageParserRegistry
{
public:
    static MessageParserRegistry &instance();

    bool add(MessageParserSpec spec);
    bool remove(const QString &id);
    void clear();

    std::optional<MessageParserSpec> find(const QString &protocolId) const;
    QVector<MessageParserSpec> all() const;
    QStringList protocolIds() const;

    std::optional<MessageParserSpec> findByExtension(const QString &suffix) const;
    std::optional<MessageParserSpec> findBySniff(const QByteArray &data) const;

    std::unique_ptr<IMessageParser> create(const QString &protocolId) const;
    bool parse(const QString &protocolId, const QByteArray &data, PsDecodedFrame &out) const;

private:
    MessageParserRegistry() = default;

    static QString normalizeProtocol(const QString &protocolId);
    static QString normalizeSuffix(const QString &suffix);
    static bool matchesExtension(const QStringList &extensions, const QString &suffix);

    mutable QReadWriteLock m_lock;
    QVector<MessageParserSpec> m_specs;
};
