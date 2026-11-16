#include "AutopilotComponent.h"
#include "AutopilotReview.h"
#include "ApbinMessageParser.h"
#include "MavlinkMessageParser.h"
#include "UlogMessageParser.h"
#include "datasource/MessageParserRegistry.h"

#include <memory>
#include <utility>

struct PsExtensionDescriptor;

extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_ulog(void);
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_apbin(void);
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_tlog(void);
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_source_sim(void);
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_source_mavlink(void);

namespace ps::components
{
namespace
{
DataSourceSpec source(const PsExtensionDescriptor *descriptor, const QString &id,
                      const QString &label, QStringList transports)
{
    DataSourceSpec spec;
    spec.descriptor = descriptor;
    spec.id = id;
    spec.label = label;
    spec.transports = std::move(transports);
    return spec;
}

struct AutopilotRegistrar
{
    AutopilotRegistrar()
    {
        ExtensionRegistry::instance().addExtension(
            ExtensionDescriptor{QStringLiteral("autopilot.core"), QStringLiteral("飞控"),
                                QStringLiteral("1.0"), ExtensionType::Builtin},
            [] { return std::make_unique<AutopilotComponent>(); });
    }
};
static AutopilotRegistrar g_autopilotRegistrar;
} // namespace

ComponentManifest AutopilotComponent::manifest() const
{
    return {QStringLiteral("autopilot.core"), QStringLiteral("飞控"), QStringLiteral("1.0"),
            {QStringLiteral("data_source")}};
}

void AutopilotComponent::contribute(Registry &registry, HostContext &host)
{
    registry.addDataSource(source(ps_extension_entry_data_loader_ulog(),
                                  QStringLiteral("data_loader_ulog"),
                                  QStringLiteral("ULog 飞控日志"), {QStringLiteral("file")}));
    registry.addDataSource(source(ps_extension_entry_data_loader_apbin(),
                                  QStringLiteral("data_loader_apbin"),
                                  QStringLiteral("ArduPilot DataFlash"),
                                  {QStringLiteral("file")}));
    registry.addDataSource(source(ps_extension_entry_data_loader_tlog(),
                                  QStringLiteral("data_loader_tlog"),
                                  QStringLiteral("MAVLink tlog"), {QStringLiteral("file")}));
    registry.addDataSource(source(ps_extension_entry_data_source_sim(),
                                  QStringLiteral("data_source_sim"),
                                  QStringLiteral("飞控仿真源"),
                                  {QStringLiteral("internal")}));
    registry.addDataSource(source(ps_extension_entry_data_source_mavlink(),
                                  QStringLiteral("data_source_mavlink"),
                                  QStringLiteral("MAVLink 遥测"),
                                  {QStringLiteral("file"), QStringLiteral("udp")}));

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

    MessageParserSpec ulog;
    ulog.id = QStringLiteral("autopilot.ulog");
    ulog.protocolId = QStringLiteral("ulog");
    ulog.displayName = QStringLiteral("ULog");
    ulog.priority = 30;
    ulog.fileExtensions = {QStringLiteral(".ulg")};
    ulog.sniff = [](const QByteArray &data) { return UlogMessageParser().sniff(data); };
    ulog.factory = [] { return std::make_unique<UlogMessageParser>(); };
    ulog.capabilities = {QStringLiteral("frame-table"), QStringLiteral("field-values")};
    MessageParserRegistry::instance().add(std::move(ulog));

    MessageParserSpec apbin;
    apbin.id = QStringLiteral("autopilot.apbin");
    apbin.protocolId = QStringLiteral("apbin");
    apbin.displayName = QStringLiteral("ArduPilot DataFlash");
    apbin.priority = 30;
    apbin.fileExtensions = {QStringLiteral(".bin"), QStringLiteral(".apbin")};
    apbin.sniff = [](const QByteArray &data) { return ApbinMessageParser().sniff(data); };
    apbin.factory = [] { return std::make_unique<ApbinMessageParser>(); };
    apbin.capabilities = {QStringLiteral("frame-table"), QStringLiteral("field-values")};
    MessageParserRegistry::instance().add(std::move(apbin));

    registerAutopilotReview(registry, host);
}

} // namespace ps::components
