#pragma once

#include "datasource/IMessageParser.h"

#include <QHash>
#include <QMap>
#include <QString>
#include <QVariant>
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
        QString countField;
        int offset = 0;
        int size = 0;
        int count = -1;
        int stride = 0;
        int bitOffset = -1;
        int bitWidth = 0;
        double scale = 1.0;
        QString enumBy;
        QMap<QString, QVariant> enumValues;
        QVector<FieldDef> children;
    };

    struct VariantRule
    {
        bool present = false;
        int offset = -1;
        QString type;
        int length = -1;
        int lengthBase = -1;
        int lengthCountOffset = -1;
        int lengthStride = -1;
        QVariant equals;
        QVariant notEquals;
    };

    struct MessageDef
    {
        QString name;
        QString timeField;
        QVector<FieldDef> fields;
        VariantRule variant;
    };

    UbxMessageParser();
    explicit UbxMessageParser(const QString &databasePath);

    static bool sniffData(const QByteArray &data);

    QString protocolId() const override;
    bool sniff(const QByteArray &data) const override;
    bool parse(const QByteArray &data, PsDecodedFrame &out) const override;

private:
    void loadDatabase();
    QHash<QString, QVector<MessageDef>> m_messages;
};
