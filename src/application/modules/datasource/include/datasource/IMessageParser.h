#pragma once

#include "DecodedFrame.h"

#include <QByteArray>
#include <QString>

class IMessageParser
{
public:
    virtual ~IMessageParser() = default;

    virtual QString protocolId() const = 0;
    virtual bool sniff(const QByteArray &data) const
    {
        (void)data;
        return false;
    }
    virtual bool parse(const QByteArray &data, PsDecodedFrame &out) const = 0;
};
