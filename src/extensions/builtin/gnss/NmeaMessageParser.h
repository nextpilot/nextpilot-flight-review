#pragma once

#include "datasource/IMessageParser.h"

#include <QHash>
#include <QString>
#include <QVector>

class NmeaMessageParser : public IMessageParser
{
public:
    struct FieldDef
    {
        QString name;
        QString type;
        QString unit;
        QString label;
        int index = -1;
        int hemiIndex = -1;
    };

    NmeaMessageParser();
    explicit NmeaMessageParser(const QString &databasePath);

    static bool sniffData(const QByteArray &data);

    QString protocolId() const override;
    bool sniff(const QByteArray &data) const override;
    bool parse(const QByteArray &data, PsDecodedFrame &out) const override;

    bool loaded() const { return !m_sentences.isEmpty(); }

private:
    void loadDatabase();

    QHash<QString, QVector<FieldDef>> m_sentences;
};
