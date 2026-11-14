#include "ProtocolDocument.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <cassert>
#include <iostream>
#include <string>

using namespace ps::protocol;

static QJsonObject readJson(const std::string &path)
{
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

static void roundtrip(const std::string &path)
{
    const QJsonObject src = readJson(path);
    assert(!src.isEmpty());
    ProtocolDocument a;
    std::string err;
    if (!parseDocument(src, a, err))
    {
        std::cerr << path << ": " << err << "\n";
        assert(false);
    }
    const QJsonObject once = toJson(a);
    ProtocolDocument b;
    if (!parseDocument(once, b, err))
    {
        std::cerr << path << " roundtrip: " << err << "\n";
        assert(false);
    }
    const QJsonObject twice = toJson(b);
    assert(once == twice);
    assert(!a.segments.empty());
    std::cout << path << ": " << a.segments.size() << " segments, "
              << a.series.variants.size() << " variants\n";
}

int main()
{
    roundtrip(std::string(PS_FIXTURE_DIR) + "/demo_sequential.psproto");
    roundtrip(std::string(PS_FIXTURE_DIR) + "/demo_nmea.psproto");
    std::cout << "protocol document ok\n";
    return 0;
}
