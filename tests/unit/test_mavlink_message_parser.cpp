#include "MavlinkMessageParser.h"
#include "minimal/minimal.hpp"

#include <cstring>

#include <QByteArray>
#include <cassert>
#include <cstdio>
#include <QtGlobal>

int main()
{
    mavlink::mavlink_message_t message{};
    mavlink::minimal::msg::HEARTBEAT heartbeat{};
    heartbeat.type = 2;
    heartbeat.autopilot = 3;
    heartbeat.base_mode = 4;
    heartbeat.custom_mode = 42;
    heartbeat.system_status = 5;
    heartbeat.mavlink_version = 3;

    mavlink::MsgMap map(message);
    heartbeat.serialize(map);
    mavlink::mavlink_finalize_message(&message, 1, 1, heartbeat.MIN_LENGTH, heartbeat.LENGTH,
                                      heartbeat.CRC_EXTRA);

    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
    const uint16_t length = mavlink::mavlink_msg_to_send_buffer(buffer, &message);
    QByteArray frame(reinterpret_cast<const char *>(buffer), length);
    frame.prepend(QByteArray(8, '\0')); // tlog timestamp prefix

    MavlinkMessageParser parser;
    PsDecodedFrame decoded;
    assert(parser.sniff(frame));
    assert(parser.parse(frame, decoded));
    assert(decoded.frameName == "HEARTBEAT");
    assert(decoded.frameKey == "0");
    assert(decoded.checksumOk);
    assert(!decoded.unknown);

    auto field = [&decoded](const QString &name) -> QVariant {
        for (const auto &item : decoded.fields)
            if (item.name == name)
                return item.value;
        return {};
    };
    assert(field("custom_mode").toInt() == 42);
    assert(field("type").toInt() == 2);
    assert(field("sysid").toInt() == 1);


    // ATT_POS_MOCAP: 验证 wire offset、数组展开和单位换算。
    mavlink::mavlink_message_t mocap{};
    mavlink::MsgMap mocapMap(mocap);
    mocapMap.reset(138, 120);
    uint8_t *mocapPayload = reinterpret_cast<uint8_t *>(mocap.payload64);
    std::memset(mocapPayload, 0, 120);
    const uint64_t timeUsec = 1000000;
    std::memcpy(mocapPayload, &timeUsec, sizeof(timeUsec));
    const float q[4] = {0.5f, 0.0f, 0.0f, 0.0f};
    std::memcpy(mocapPayload + 8, q, sizeof(q));
    const float x = 1.5f;
    std::memcpy(mocapPayload + 24, &x, sizeof(x));
    mavlink::mavlink_finalize_message(&mocap, 1, 1, 120, 120, 109);
    uint8_t mocapBuffer[MAVLINK_MAX_PACKET_LEN];
    const uint16_t mocapLength = mavlink::mavlink_msg_to_send_buffer(mocapBuffer, &mocap);
    QByteArray mocapFrame(reinterpret_cast<const char *>(mocapBuffer), mocapLength);
    PsDecodedFrame mocapDecoded;
    assert(parser.parse(mocapFrame, mocapDecoded));
    assert(mocapDecoded.frameName == "ATT_POS_MOCAP");
    auto mocapField = [&mocapDecoded](const QString &name) -> PsDecodedField {
        for (const auto &item : mocapDecoded.fields)
            if (item.name == name)
                return item;
        return {};
    };
    assert(mocapField("time_usec").unit == "s");
    assert(qFuzzyCompare(mocapField("time_usec").value.toDouble(), 1.0));
    assert(mocapField("q[0]").value.toDouble() == 0.5);
    assert(mocapField("x").unit == "m");
    assert(mocapField("x").value.toDouble() == 1.5);

    std::printf("mavlink message parser ok\n");
    return 0;
}
