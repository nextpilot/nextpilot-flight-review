#include "Component.h"
#include "datasource/MessageParserRegistry.h"
#include "datasource/AnalysisRegistry.h"

#include <QSet>
#include <QStringList>
#include <cassert>
#include <iostream>

using namespace ps::components;

namespace
{
const ExtensionEntry *findExtension(const QString &id)
{
    for (const ExtensionEntry &entry : ExtensionRegistry::instance().extensions())
    {
        if (entry.descriptor.id == id)
            return &entry;
    }
    return nullptr;
}

QSet<QString> dataSourceIds(const QString &componentId)
{
    const ExtensionEntry *entry = findExtension(componentId);
    assert(entry && entry->factory);
    auto component = entry->factory();
    assert(component);

    Registry registry;
    HostContext host;
    component->contribute(registry, host);

    QSet<QString> ids;
    for (const DataSourceSpec &source : registry.dataSources())
        ids.insert(source.id);
    return ids;
}

void expect(const QSet<QString> &ids, const QStringList &expected, const char *domain)
{
    for (const QString &id : expected)
    {
        if (!ids.contains(id))
        {
            std::cerr << "missing " << domain << " source: " << id.toStdString() << "\n";
            assert(false);
        }
    }
}
} // namespace

int main()
{
    const QStringList domains = {QStringLiteral("autopilot.core"),
                                 QStringLiteral("automotive.core"),
                                 QStringLiteral("robotics.core")};
    for (const QString &id : domains)
        assert(findExtension(id));

    const QStringList autopilot = {QStringLiteral("data_loader_ulog"),
                                   QStringLiteral("data_loader_apbin"),
                                   QStringLiteral("data_loader_tlog"),
                                   QStringLiteral("data_source_sim"),
                                   QStringLiteral("data_source_mavlink")};
    expect(dataSourceIds(QStringLiteral("autopilot.core")), autopilot, "autopilot");
    for (const QString &protocol : {QStringLiteral("mavlink"), QStringLiteral("ulog"),
                                    QStringLiteral("apbin")})
    {
        if (!MessageParserRegistry::instance().protocolIds().contains(protocol))
        {
            std::cerr << "missing autopilot parser: " << protocol.toStdString() << "\n";
            assert(false);
        }
    }
    for (const QString &id : {QStringLiteral("autopilot.review.default"),
                              QStringLiteral("autopilot.review.report")})
    {
        bool found = false;
        for (const auto &entry : ps::data_source::AnalysisRegistry::instance().all())
            found = found || entry.id == id;
        if (!found)
        {
            std::cerr << "missing analysis entry: " << id.toStdString() << "\n";
            assert(false);
        }
    }
    bool foundApp = false;
    for (const auto &entry : ps::data_source::AnalysisRegistry::instance().all())
    {
        if (entry.id == QStringLiteral("autopilot.review.app"))
        {
            foundApp = entry.kind == ps::data_source::AnalysisKind::App && entry.create;
            break;
        }
    }
    if (!foundApp)
    {
        std::cerr << "missing analysis app: autopilot.review.app\n";
        assert(false);
    }
    const QStringList automotive = {QStringLiteral("data_loader_asc"),
                                    QStringLiteral("data_loader_blf"),
                                    QStringLiteral("data_loader_mdf4")};
    expect(dataSourceIds(QStringLiteral("automotive.core")), automotive, "automotive");

    const QStringList robotics = {QStringLiteral("data_loader_rosbag2"),
                                  QStringLiteral("data_load_mcap")};
    expect(dataSourceIds(QStringLiteral("robotics.core")), robotics, "robotics");

    const QStringList generic = {QStringLiteral("data_loader_csv"),
                                 QStringLiteral("data_loader_json"),
                                 QStringLiteral("data_loader_binary")};
    expect(dataSourceIds(QStringLiteral("data.builtin")), generic, "builtin");

    if (findExtension(QStringLiteral("gnss.core")))
    {
        const QStringList gnss = {QStringLiteral("data_loader_nmea"),
                                  QStringLiteral("data_loader_ubx"),
                                  QStringLiteral("data_source_oem7")};
        expect(dataSourceIds(QStringLiteral("gnss.core")), gnss, "gnss");
    }

    assert(MessageParserRegistry::instance().find(QStringLiteral("mavlink")));
    assert(MessageParserRegistry::instance().find(QStringLiteral("nmea")));
    assert(MessageParserRegistry::instance().find(QStringLiteral("ubx")));
    assert(MessageParserRegistry::instance().find(QStringLiteral("oem7")));

    std::cout << "toolbox registration ok\n";
    return 0;
}
