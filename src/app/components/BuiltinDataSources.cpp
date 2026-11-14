#include "BuiltinDataSources.h"

struct PsPluginDescriptor;

extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_csv(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_ulog(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_apbin(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_asc(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_blf(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_nmea(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_ubx(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_json(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_binary(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_mdf4(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_rosbag2(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_load_mcap(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_source_sim(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_source_mavlink(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_source_oem7(void);

namespace ps::components
{
namespace
{

DataSourceSpec source(const PsPluginDescriptor *descriptor, QStringList transports)
{
    DataSourceSpec spec;
    spec.descriptor = descriptor;
    spec.transports = std::move(transports);
    return spec;
}

} // namespace

ComponentManifest BuiltinDataSourcesComponent::manifest() const
{
    return {QStringLiteral("data.builtin"), QStringLiteral("内建数据源"), QStringLiteral("1.0"),
            {QStringLiteral("data_source")}};
}

void BuiltinDataSourcesComponent::contribute(Registry &registry, const HostServices &)
{
    registry.addDataSource(source(ps_plugin_entry_data_loader_csv(), {QStringLiteral("file")}));
    registry.addDataSource(source(ps_plugin_entry_data_loader_ulog(), {QStringLiteral("file")}));
    registry.addDataSource(source(ps_plugin_entry_data_loader_apbin(), {QStringLiteral("file")}));
    registry.addDataSource(source(ps_plugin_entry_data_loader_asc(), {QStringLiteral("file")}));
    registry.addDataSource(source(ps_plugin_entry_data_loader_blf(), {QStringLiteral("file")}));
    registry.addDataSource(source(ps_plugin_entry_data_loader_nmea(),
                                  {QStringLiteral("file"), QStringLiteral("serial"),
                                   QStringLiteral("tcp")}));
    registry.addDataSource(source(ps_plugin_entry_data_loader_ubx(),
                                  {QStringLiteral("file"), QStringLiteral("serial")}));
    registry.addDataSource(source(ps_plugin_entry_data_loader_json(), {QStringLiteral("file")}));
    registry.addDataSource(source(ps_plugin_entry_data_loader_binary(), {QStringLiteral("file")}));
    registry.addDataSource(source(ps_plugin_entry_data_loader_mdf4(), {QStringLiteral("file")}));
    registry.addDataSource(source(ps_plugin_entry_data_loader_rosbag2(), {QStringLiteral("file")}));
    registry.addDataSource(source(ps_plugin_entry_data_load_mcap(), {QStringLiteral("file")}));
    registry.addDataSource(source(ps_plugin_entry_data_source_sim(), {QStringLiteral("internal")}));
    registry.addDataSource(source(ps_plugin_entry_data_source_mavlink(),
                                  {QStringLiteral("file"), QStringLiteral("serial"),
                                   QStringLiteral("tcp"), QStringLiteral("udp")}));
    registry.addDataSource(source(ps_plugin_entry_data_source_oem7(),
                                  {QStringLiteral("file"), QStringLiteral("serial"),
                                   QStringLiteral("tcp")}));
}

} // namespace ps::components
