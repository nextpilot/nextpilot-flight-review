#include "GnssComponent.h"
#include "NmeaMessageParser.h"
#include "Oem7MessageParser.h"
#include "UbxMessageParser.h"
#include "datasource/MessageParserRegistry.h"

#include <utility>

struct PsExtensionDescriptor;

extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_nmea(void);
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_ubx(void);
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_source_oem7(void);

namespace ps::components
{
namespace
{
struct GnssRegistrar
{
    GnssRegistrar()
    {
        ExtensionRegistry::instance().addExtension(
            ExtensionDescriptor{QStringLiteral("gnss.core"), QStringLiteral("GNSS"),
                                QStringLiteral("1.0"), ExtensionType::Builtin},
            [] { return std::make_unique<GnssComponent>(); });
    }
};
static GnssRegistrar g_gnssRegistrar;
} // namespace

ComponentManifest GnssComponent::manifest() const
{
    return {QStringLiteral("gnss.core"), QStringLiteral("GNSS"), QStringLiteral("1.0"),
            {QStringLiteral("data_source")}};
}

void GnssComponent::contribute(Registry &registry, HostContext &)
{
    DataSourceSpec nmea;
    nmea.descriptor = ps_extension_entry_data_loader_nmea();
    nmea.id = QStringLiteral("data_loader_nmea");
    nmea.label = QStringLiteral("NMEA");
    nmea.transports = {QStringLiteral("file"), QStringLiteral("serial"), QStringLiteral("tcp")};
    registry.addDataSource(nmea);

    DataSourceSpec ubx;
    ubx.descriptor = ps_extension_entry_data_loader_ubx();
    ubx.id = QStringLiteral("data_loader_ubx");
    ubx.label = QStringLiteral("UBX");
    ubx.transports = {QStringLiteral("file"), QStringLiteral("serial")};
    registry.addDataSource(ubx);

    DataSourceSpec oem7;
    oem7.descriptor = ps_extension_entry_data_source_oem7();
    oem7.id = QStringLiteral("data_source_oem7");
    oem7.label = QStringLiteral("NovAtel OEM7");
    oem7.transports = {QStringLiteral("file"), QStringLiteral("serial"), QStringLiteral("tcp")};
    registry.addDataSource(oem7);

    MessageParserSpec nmeaParser;
    nmeaParser.id = QStringLiteral("gnss.nmea");
    nmeaParser.protocolId = QStringLiteral("nmea");
    nmeaParser.displayName = QStringLiteral("NMEA");
    nmeaParser.priority = 10;
    nmeaParser.fileExtensions = {QStringLiteral(".nmea"), QStringLiteral(".txt")};
    nmeaParser.sniff = [](const QByteArray &data) { return NmeaMessageParser::sniffData(data); };
    nmeaParser.factory = [] { return std::make_unique<NmeaMessageParser>(); };
    MessageParserRegistry::instance().add(std::move(nmeaParser));

    MessageParserSpec ubxParser;
    ubxParser.id = QStringLiteral("gnss.ubx");
    ubxParser.protocolId = QStringLiteral("ubx");
    ubxParser.displayName = QStringLiteral("UBX");
    ubxParser.priority = 10;
    ubxParser.fileExtensions = {QStringLiteral(".ubx")};
    ubxParser.sniff = [](const QByteArray &data) { return UbxMessageParser::sniffData(data); };
    ubxParser.factory = [] { return std::make_unique<UbxMessageParser>(); };
    MessageParserRegistry::instance().add(std::move(ubxParser));

    MessageParserSpec oem7Parser;
    oem7Parser.id = QStringLiteral("gnss.oem7");
    oem7Parser.protocolId = QStringLiteral("oem7");
    oem7Parser.displayName = QStringLiteral("NovAtel OEM7");
    oem7Parser.priority = 10;
    oem7Parser.fileExtensions = {QStringLiteral(".bin"), QStringLiteral(".gps"),
                                QStringLiteral(".log"), QStringLiteral(".dat"),
                                QStringLiteral(".psraw"), QStringLiteral(".raw")};
    oem7Parser.sniff = [](const QByteArray &data) { return Oem7MessageParser::sniffData(data); };
    oem7Parser.factory = [] { return std::make_unique<Oem7MessageParser>(); };
    MessageParserRegistry::instance().add(std::move(oem7Parser));
}

} // namespace ps::components
