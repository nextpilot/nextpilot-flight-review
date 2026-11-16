#include "BuiltinDataSources.h"

struct PsExtensionDescriptor;

extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_csv(void);
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_ulog(void);
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_apbin(void);
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_asc(void);
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_blf(void);
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_json(void);
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_binary(void);
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_mdf4(void);
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_rosbag2(void);
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_load_mcap(void);
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_source_sim(void);
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_source_mavlink(void);

namespace ps::components
{
namespace
{
struct BuiltinSourcesRegistrar
{
    BuiltinSourcesRegistrar()
    {
        ExtensionRegistry::instance().addExtension(
            ExtensionDescriptor{QStringLiteral("data.builtin"), QStringLiteral("内建数据源"),
                                QStringLiteral("1.0"), ExtensionType::Builtin},
            [] { return std::make_unique<BuiltinDataSourcesComponent>(); });
    }
};
static BuiltinSourcesRegistrar g_builtinSourcesRegistrar;

DataSourceSpec source(const PsExtensionDescriptor *descriptor, QStringList transports)
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

void BuiltinDataSourcesComponent::contribute(Registry &registry, HostContext &)
{
    registry.addDataSource(source(ps_extension_entry_data_loader_csv(), {QStringLiteral("file")}));
    registry.addDataSource(source(ps_extension_entry_data_loader_ulog(), {QStringLiteral("file")}));
    registry.addDataSource(source(ps_extension_entry_data_loader_apbin(), {QStringLiteral("file")}));
    registry.addDataSource(source(ps_extension_entry_data_loader_asc(), {QStringLiteral("file")}));
    registry.addDataSource(source(ps_extension_entry_data_loader_blf(), {QStringLiteral("file")}));
    registry.addDataSource(source(ps_extension_entry_data_loader_json(), {QStringLiteral("file")}));
    registry.addDataSource(source(ps_extension_entry_data_loader_binary(), {QStringLiteral("file")}));
    registry.addDataSource(source(ps_extension_entry_data_loader_mdf4(), {QStringLiteral("file")}));
    registry.addDataSource(source(ps_extension_entry_data_loader_rosbag2(), {QStringLiteral("file")}));
    registry.addDataSource(source(ps_extension_entry_data_load_mcap(), {QStringLiteral("file")}));
    registry.addDataSource(source(ps_extension_entry_data_source_sim(), {QStringLiteral("internal")}));
    registry.addDataSource(source(ps_extension_entry_data_source_mavlink(),
                                  {QStringLiteral("file"), QStringLiteral("serial"),
                                   QStringLiteral("tcp"), QStringLiteral("udp")}));
}

} // namespace ps::components
