#include "datasource/MessageParserRegistry.h"

#include <QByteArray>
#include <QString>

#include <cassert>
#include <memory>
#include <utility>

namespace
{

class StubParser : public IMessageParser
{
public:
    explicit StubParser(QString protocol)
        : m_protocol(std::move(protocol))
    {
    }

    QString protocolId() const override { return m_protocol; }

    bool sniff(const QByteArray &data) const override
    {
        return data.startsWith("STUB");
    }

    bool parse(const QByteArray &data, PsDecodedFrame &out) const override
    {
        out.frameName = QStringLiteral("stub");
        out.rawBytes = data;
        return true;
    }

private:
    QString m_protocol;
};

MessageParserSpec makeSpec(const QString &id, const QString &protocol, int priority,
                           const QStringList &extensions = {},
                           std::function<bool(const QByteArray &)> sniff = {})
{
    MessageParserSpec spec;
    spec.id = id;
    spec.protocolId = protocol;
    spec.displayName = id;
    spec.priority = priority;
    spec.fileExtensions = extensions;
    spec.sniff = std::move(sniff);
    spec.factory = [protocol] { return std::make_unique<StubParser>(protocol); };
    return spec;
}

void testRegistration()
{
    auto &registry = MessageParserRegistry::instance();
    registry.clear();

    assert(registry.add(makeSpec(QStringLiteral("mavlink.low"), QStringLiteral("MAVLink"), 10,
                                  {QStringLiteral("tlog")})));
    assert(registry.add(makeSpec(QStringLiteral("mavlink.high"), QStringLiteral("mavlink"), 50,
                                  {QStringLiteral(".tlog")},
                                  [](const QByteArray &data) { return data.startsWith("MAV"); })));

    auto found = registry.find(QStringLiteral("MAVLINK"));
    assert(found.has_value());
    assert(found->id == QStringLiteral("mavlink.high"));

    const auto all = registry.all();
    assert(all.size() == 2);
    assert(all.at(0).id == QStringLiteral("mavlink.high"));
    assert(all.at(1).id == QStringLiteral("mavlink.low"));

    const auto protocols = registry.protocolIds();
    assert(protocols.size() == 1);
    assert(protocols.at(0) == QStringLiteral("mavlink"));

    assert(!registry.add(makeSpec(QStringLiteral("mavlink.high"), QStringLiteral("mavlink"), 99)));
    assert(!registry.add(makeSpec(QString(), QStringLiteral("mavlink"), 1)));
    assert(!registry.add(makeSpec(QStringLiteral("bad"), QString(), 1)));

    assert(registry.findByExtension(QStringLiteral("TLOG")).has_value());
    assert(registry.findByExtension(QStringLiteral(".tlog"))->id == QStringLiteral("mavlink.high"));
    assert(registry.findBySniff(QByteArray("MAV"))->id == QStringLiteral("mavlink.high"));
    assert(registry.findBySniff(QByteArray("STUB"))->id == QStringLiteral("mavlink.low"));
    assert(!registry.findBySniff(QByteArray("NMEA")).has_value());

    PsDecodedFrame frame;
    assert(registry.parse(QStringLiteral("MAVLink"), QByteArray("MAV"), frame));
    assert(frame.protocol == QStringLiteral("mavlink"));
    assert(frame.frameName == QStringLiteral("stub"));
    assert(frame.rawBytes == QByteArray("MAV"));

    assert(registry.create(QStringLiteral("mavlink")) != nullptr);
    assert(registry.create(QStringLiteral("unknown")) == nullptr);
    assert(!registry.parse(QStringLiteral("unknown"), QByteArray("x"), frame));

    assert(registry.remove(QStringLiteral("mavlink.high")));
    assert(!registry.remove(QStringLiteral("mavlink.high")));
    assert(registry.find(QStringLiteral("mavlink"))->id == QStringLiteral("mavlink.low"));

    registry.clear();
    assert(registry.all().isEmpty());
}

} // namespace

int main()
{
    testRegistration();
    return 0;
}
