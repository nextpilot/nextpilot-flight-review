#include "CommonDataSourcesComponent.h"

struct PsExtensionDescriptor;

extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_csv(void);
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_json(void);
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_binary(void);

namespace ps::components
{
namespace
{
struct CommonSourcesRegistrar
{
    CommonSourcesRegistrar()
    {
        ExtensionRegistry::instance().addExtension(
            ExtensionDescriptor{QStringLiteral("data.builtin"), QStringLiteral("通用数据源"),
                                QStringLiteral("1.0"), ExtensionType::Builtin},
            [] { return std::make_unique<CommonDataSourcesComponent>(); });
    }
};
static CommonSourcesRegistrar g_commonSourcesRegistrar;

DataSourceSpec source(const PsExtensionDescriptor *descriptor, const QString &id,
                      const QString &label)
{
    DataSourceSpec spec;
    spec.descriptor = descriptor;
    spec.id = id;
    spec.label = label;
    spec.transports = {QStringLiteral("file")};
    return spec;
}

} // namespace

ComponentManifest CommonDataSourcesComponent::manifest() const
{
    return {QStringLiteral("data.builtin"), QStringLiteral("通用数据源"), QStringLiteral("1.0"),
            {QStringLiteral("data_source")}};
}

void CommonDataSourcesComponent::contribute(Registry &registry, HostContext &)
{
    registry.addDataSource(source(ps_extension_entry_data_loader_csv(),
                                  QStringLiteral("data_loader_csv"), QStringLiteral("CSV")));
    registry.addDataSource(source(ps_extension_entry_data_loader_json(),
                                  QStringLiteral("data_loader_json"), QStringLiteral("JSON")));
    registry.addDataSource(source(ps_extension_entry_data_loader_binary(),
                                  QStringLiteral("data_loader_binary"),
                                  QStringLiteral("二进制")));
}

} // namespace ps::components
