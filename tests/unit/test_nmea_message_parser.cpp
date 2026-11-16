#include "NmeaMessageParser.h"

#include <QByteArray>
#include <QString>

#include <QtGlobal>

#include <cassert>

namespace
{

void testFrame()
{
    NmeaMessageParser parser;
    assert(parser.protocolId() == QStringLiteral("nmea"));
    assert(parser.sniff(QByteArray("$GPGGA,1*00\r\n")));
    assert(!parser.sniff(QByteArray("GPGGA,1*00\r\n")));

    const QByteArray line = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
    PsDecodedFrame frame;
    assert(parser.parse(line, frame));
    assert(frame.protocol == QStringLiteral("nmea"));
    assert(frame.frameKey == QStringLiteral("GPGGA"));
    assert(frame.frameName == QStringLiteral("GPGGA"));
    assert(frame.checksumOk);
    assert(!frame.unknown);
    assert(frame.rawBytes == line);

    assert(frame.fields.size() >= 4);
    assert(frame.fields.at(0).name == QStringLiteral("talker"));
    assert(frame.fields.at(0).value.toString() == QStringLiteral("GP"));
    assert(frame.fields.at(1).name == QStringLiteral("sentence"));
    assert(frame.fields.at(1).value.toString() == QStringLiteral("GGA"));
    assert(frame.fields.at(2).name == QStringLiteral("utc"));
    assert(frame.fields.at(2).value.toString() == QStringLiteral("123519"));
    assert(frame.fields.at(3).name == QStringLiteral("latitude"));
    assert(qAbs(frame.fields.at(3).value.toDouble() - 48.1173) < 0.0001);
    assert(frame.fields.last().name == QStringLiteral("checksum"));

    PsDecodedFrame bad;
    assert(parser.parse("$GPGGA,1*00", bad));
    assert(!bad.checksumOk);

    PsDecodedFrame invalid;
    assert(!parser.parse("GPGGA,1*00", invalid));

    PsDecodedFrame unknown;
    assert(parser.parse("$PGRMZ,100,f*00", unknown));
    assert(unknown.unknown);
    assert(unknown.fields.at(2).name == QStringLiteral("field_1"));
}

void testFirstLine()
{
    NmeaMessageParser parser;
    PsDecodedFrame frame;
    assert(parser.parse("$GPRMC,1*00\r\n$GPGGA,2*00\r\n", frame));
    assert(frame.frameKey == QStringLiteral("GPRMC"));
}

} // namespace

int main()
{
    testFrame();
    testFirstLine();
    return 0;
}
