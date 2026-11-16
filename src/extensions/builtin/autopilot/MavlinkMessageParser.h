#pragma once

#include "datasource/IMessageParser.h"

#ifndef MAVLINK_STX
#define MAVLINK_STX 253
#endif

#include "message.hpp"

class MavlinkMessageParser : public IMessageParser
{
public:
    QString protocolId() const override;
    bool sniff(const QByteArray &data) const override;
    bool parse(const QByteArray &data, PsDecodedFrame &out) const override;

private:
    mutable mavlink::mavlink_message_t m_rx{};
    mutable mavlink::mavlink_status_t m_status{};
};
