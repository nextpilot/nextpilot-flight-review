#pragma once

#include "datasource/IMessageParser.h"

class UbxMessageParser : public IMessageParser
{
public:
    QString protocolId() const override;
    bool sniff(const QByteArray &data) const override;
    bool parse(const QByteArray &data, PsDecodedFrame &out) const override;
};
