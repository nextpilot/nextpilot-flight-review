#include "AutopilotComponent.h"
#include "AutopilotReview.h"
#include "ApbinMessageParser.h"
#include "MavlinkMessageParser.h"
#include "UlogMessageParser.h"
#include "datasource/MessageParserRegistry.h"
#include "analysis/AnalysisRegistry.h"
#include "DataSourceModule.h"
#include "MainWindow.h"

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

    const QStringList analysisFormats = {
        QStringLiteral("ULog"), QStringLiteral("ArduPilot · BIN"),
        QStringLiteral("MAVLink · tlog"), QStringLiteral("MAVLink 遥测")};
    auto canOpen = [analysisFormats](QWidget *parent) {
        auto *mw = qobject_cast<MainWindow *>(parent);
        if (!mw)
            return false;
        return analysisFormats.contains(ps::components::dataSourceActiveFormat(mw),
                                      Qt::CaseInsensitive);
    };
    auto addEntry = [&](const QString &id, const QString &title, const QString &icon,
                        const QString &description,
                        std::function<QWidget *(QWidget *)> create) {
        ps::analysis::AnalysisEntry entry;
        entry.id = id;
        entry.title = title;
        entry.icon = icon;
        entry.description = description;
        entry.tags = {QStringLiteral("飞控")};
        entry.kind = create ? ps::analysis::AnalysisKind::App
                            : ps::analysis::AnalysisKind::Action;
        entry.canOpen = canOpen;
        entry.create = std::move(create);
        ps::analysis::AnalysisRegistry::instance().add(entry);
    };

    addEntry(QStringLiteral("autopilot.review.default"), QStringLiteral("默认曲线"),
             QStringLiteral("datasource"), QStringLiteral("按飞控日志自动创建姿态/位置/电源等默认曲线"),
             {});
    addEntry(QStringLiteral("autopilot.review.report"), QStringLiteral("分析报告"),
             QStringLiteral("export_data"), QStringLiteral("生成飞控通道统计与基础飞行摘要"),
             {});
    addEntry(QStringLiteral("autopilot.review.app"), QStringLiteral("飞控复盘"),
             QStringLiteral("datasource"), QStringLiteral("打开飞控复盘面板，查看通道统计并生成曲线/报告"),
             [](QWidget *parent) {
                 auto *mw = qobject_cast<MainWindow *>(parent);
                 return createFlightReviewApp(parent,
                                             mw ? ps::components::dataSourceActivePath(mw) : QString());
             });

    registerAutopilotReview(registry, host);
}

} // namespace ps::components
