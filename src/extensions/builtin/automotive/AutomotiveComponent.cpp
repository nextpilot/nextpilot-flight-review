#include "AutomotiveComponent.h"

#include <utility>

struct PsExtensionDescriptor;

extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_asc(void);
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_blf(void);
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_mdf4(void);

namespace ps::components
{
namespace
{
struct AutomotiveRegistrar
{
    AutomotiveRegistrar()
    {
        ExtensionRegistry::instance().addExtension(
            ExtensionDescriptor{QStringLiteral("automotive.core"), QStringLiteral("汽车"),
                                QStringLiteral("1.0"), ExtensionType::Builtin},
            [] { return std::make_unique<AutomotiveComponent>(); });
    }
};
static AutomotiveRegistrar g_automotiveRegistrar;

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

ComponentManifest AutomotiveComponent::manifest() const
{
    return {QStringLiteral("automotive.core"), QStringLiteral("汽车"), QStringLiteral("1.0"),
            {QStringLiteral("data_source")}};
}

void AutomotiveComponent::contribute(Registry &registry, HostContext &)
{
    registry.addDataSource(source(ps_extension_entry_data_loader_asc(),
                                  QStringLiteral("data_loader_asc"),
                                  QStringLiteral("Vector ASC")));
    registry.addDataSource(source(ps_extension_entry_data_loader_blf(),
                                  QStringLiteral("data_loader_blf"),
                                  QStringLiteral("Vector BLF")));
    registry.addDataSource(source(ps_extension_entry_data_loader_mdf4(),
                                  QStringLiteral("data_loader_mdf4"),
                                  QStringLiteral("MDF4")));
}

} // namespace ps::components
