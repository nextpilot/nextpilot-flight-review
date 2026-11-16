#pragma once

#include "IMessageParser.h"

#include <QHash>
#include <QString>
#include <QVector>

class ApbinMessageParser : public IMessageParser
{
public:
    QString protocolId() const override { return QStringLiteral("apbin"); }
    bool sniff(const QByteArray &data) const override;
    bool parse(const QByteArray &data, PsDecodedFrame &out) const override;

private:
    struct Field
    {
        QString name;
        char code = 0;
        int offset = 0;
        int size = 0;
        bool text = false;
    };
    struct Format
    {
        quint8 id = 0;
        QString name;
        int length = 0;
        QVector<Field> fields;
    };

    void parseFormat(const QByteArray &packet) const;
    mutable QHash<quint8, Format> m_formats;
};
