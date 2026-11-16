#pragma once

#include "datasource/IMessageParser.h"

#include <QHash>
#include <QString>
#include <QVector>

class UbxMessageParser : public IMessageParser
{
public:
    struct FieldDef
    {
        QString name;
        QString type;
        QString unit;
        QString description;
        int offset = 0;
        double scale = 1.0;
    };

    UbxMessageParser();
    explicit UbxMessageParser(const QString &databasePath);

    static bool sniffData(const QByteArray &data);

    QString protocolId() const override;
    bool sniff(const QByteArray &data) const override;
    bool parse(const QByteArray &data, PsDecodedFrame &out) const override;

private:
    void loadDatabase();
    QHash<QString, QVector<FieldDef>> m_messages;
    QHash<QString, QString> m_names;
};
