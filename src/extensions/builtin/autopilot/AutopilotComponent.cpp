#include "AutopilotComponent.h"
#include "MavlinkMessageParser.h"
#include "datasource/MessageParserRegistry.h"

#include <memory>
#include <utility>

namespace ps::components
{
namespace
{
struct AutopilotRegistrar
{
    AutopilotRegistrar()
    {
        ExtensionRegistry::instance().addExtension(
            ExtensionDescriptor{QStringLiteral("autopilot.core"), QStringLiteral("Autopilot"),
                                QStringLiteral("1.0"), ExtensionType::Builtin},
            [] { return std::make_unique<AutopilotComponent>(); });
    }
};
static AutopilotRegistrar g_autopilotRegistrar;
} // namespace

ComponentManifest AutopilotComponent::manifest() const
{
    return {QStringLiteral("autopilot.core"), QStringLiteral("Autopilot"), QStringLiteral("1.0"),
            {QStringLiteral("data_source")}};
}

void AutopilotComponent::contribute(Registry &, HostContext &)
{
    MessageParserSpec mavlink;
    mavlink.id = QStringLiteral("autopilot.mavlink");
    mavlink.protocolId = QStringLiteral("mavlink");
    mavlink.displayName = QStringLiteral("MAVLink");
    mavlink.priority = 20;
    mavlink.fileExtensions = {QStringLiteral(".tlog"), QStringLiteral(".mavlink"),
                              QStringLiteral(".mavlink2")};
    mavlink.sniff = [](const QByteArray &data) { return MavlinkMessageParser().sniff(data); };
    mavlink.factory = [] { return std::make_unique<MavlinkMessageParser>(); };
    mavlink.descriptorPaths = {QStringLiteral("mavlink_metadata.json")};
    mavlink.capabilities = {QStringLiteral("full-protocol"), QStringLiteral("field-units"),
                            QStringLiteral("field-enums")};
    MessageParserRegistry::instance().add(std::move(mavlink));
}

} // namespace ps::components
