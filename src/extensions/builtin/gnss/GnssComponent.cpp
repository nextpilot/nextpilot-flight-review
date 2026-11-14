#include "GnssComponent.h"

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

void GnssComponent::contribute(Registry &registry, const HostServices &)
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
}

} // namespace ps::components
