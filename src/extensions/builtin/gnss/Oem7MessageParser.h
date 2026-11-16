#pragma once

#include "datasource/IMessageParser.h"
#include "datasource/ProtocolCatalog.h"

#include <QString>

class Oem7MessageParser : public IMessageParser
{
public:
    Oem7MessageParser();
    explicit Oem7MessageParser(const QString &databasePath);

    bool loadDatabase(const QString &path);

    static bool sniffData(const QByteArray &data);

    QString protocolId() const override;
    bool sniff(const QByteArray &data) const override;
    bool parse(const QByteArray &data, PsDecodedFrame &out) const override;

private:
    ProtocolCatalog m_catalog;
    bool m_loaded = false;
};
