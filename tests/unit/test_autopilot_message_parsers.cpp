#include "ApbinMessageParser.h"
#include "UlogMessageParser.h"

#include <QCoreApplication>
#include <QFile>
#include <QString>

#include <cassert>
#include <cstdio>

#ifndef PS_FIXTURE_DIR
#error PS_FIXTURE_DIR must be defined
#endif

static int parseAll(const QString &path, IMessageParser &parser, bool &sawData, bool &sawFormat,
                    bool &sawValue)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    QByteArray rest = file.readAll();
    int count = 0;
    while (!rest.isEmpty() && count < 5000)
    {
        PsDecodedFrame frame;
        if (!parser.parse(rest, frame) || frame.rawBytes.isEmpty())
            break;
        const int at = rest.indexOf(frame.rawBytes);
        if (at < 0)
            break;
        rest.remove(0, at + frame.rawBytes.size());
        ++count;
        if (frame.frameKey.startsWith(QStringLiteral("D:")) ||
            (!frame.frameName.isEmpty() && frame.frameName != QLatin1String("FMT")))
            sawData = true;
        if (frame.frameName == QLatin1String("FORMAT") ||
            frame.frameName == QLatin1String("FMT"))
            sawFormat = true;
        for (const PsDecodedField &field : frame.fields)
            if (field.offset >= 0 && field.value.isValid() && !field.value.toString().isEmpty())
                sawValue = true;
    }
    return count;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString fixture = QStringLiteral(PS_FIXTURE_DIR);

    UlogMessageParser ulog;
    bool ulogData = false, ulogFormat = false, ulogValue = false;
    const int ulogFrames = parseAll(fixture + QStringLiteral("/demo_flight.ulg"), ulog,
                                    ulogData, ulogFormat, ulogValue);
    if (ulogFrames == 0)
    {
        std::fprintf(stderr, "ULog parser produced no frames\n");
        return 1;
    }
    assert(ulogFormat);
    assert(ulogData);
    assert(ulogValue);

    ApbinMessageParser apbin;
    bool apbinData = false, apbinFormat = false, apbinValue = false;
    const int apbinFrames = parseAll(fixture + QStringLiteral("/demo_apbin.bin"), apbin,
                                     apbinData, apbinFormat, apbinValue);
    if (apbinFrames == 0)
    {
        std::fprintf(stderr, "apbin parser produced no frames\n");
        return 1;
    }
    assert(apbinFormat);
    assert(apbinData);
    assert(apbinValue);

    std::printf("autopilot message parsers ok: ulog=%d apbin=%d\n", ulogFrames, apbinFrames);
    return 0;
}
