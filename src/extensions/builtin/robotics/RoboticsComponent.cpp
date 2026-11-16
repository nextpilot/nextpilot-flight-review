#include "RoboticsComponent.h"

#include <utility>

struct PsExtensionDescriptor;

extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_rosbag2(void);
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_load_mcap(void);

namespace ps::components
{
namespace
{
struct RoboticsRegistrar
{
    RoboticsRegistrar()
    {
        ExtensionRegistry::instance().addExtension(
            ExtensionDescriptor{QStringLiteral("robotics.core"), QStringLiteral("机器人"),
                                QStringLiteral("1.0"), ExtensionType::Builtin},
            [] { return std::make_unique<RoboticsComponent>(); });
    }
};
static RoboticsRegistrar g_roboticsRegistrar;

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

ComponentManifest RoboticsComponent::manifest() const
{
    return {QStringLiteral("robotics.core"), QStringLiteral("机器人"), QStringLiteral("1.0"),
            {QStringLiteral("data_source")}};
}

void RoboticsComponent::contribute(Registry &registry, HostContext &)
{
    registry.addDataSource(source(ps_extension_entry_data_loader_rosbag2(),
                                  QStringLiteral("data_loader_rosbag2"),
                                  QStringLiteral("ROS 2 bag")));
    registry.addDataSource(source(ps_extension_entry_data_load_mcap(),
                                  QStringLiteral("data_load_mcap"),
                                  QStringLiteral("MCAP")));
}

} // namespace ps::components
