#pragma once

#include "IMessageParser.h"

#include <QHash>
#include <QString>
#include <QVector>

#include <memory>

class UlogMessageParser : public IMessageParser
{
public:
    QString protocolId() const override { return QStringLiteral("ulog"); }
    bool sniff(const QByteArray &data) const override;
    bool parse(const QByteArray &data, PsDecodedFrame &out) const override;

private:
    struct Field
    {
        QString name;
        QString type;
        int offset = 0;
        int size = 0;
        int count = 1;
        bool text = false;
    };
    struct Format
    {
        QString name;
        QVector<Field> fields;
        int payloadSize = 0;
    };

    void parseFormat(const QString &definition) const;
    bool appendData(const QByteArray &body, PsDecodedFrame &out) const;

    mutable QHash<QString, Format> m_formatsByName;
    mutable QHash<quint32, QString> m_formatByMsgId;
};
