#include "DeviceDebugComponent.h"

#include "DeviceDebugDialog.h"

#include "host/extension/DataInputRegistry.h"

#include <QVariantMap>

namespace ps::components
{
namespace
{
struct DeviceDebugRegistrar
{
    DeviceDebugRegistrar()
    {
        ExtensionRegistry::instance().addExtension(
            ExtensionDescriptor{QStringLiteral("device.debug"), QStringLiteral("设备调试"),
                                QStringLiteral("1.0"), ExtensionType::Builtin},
            [] { return std::make_unique<DeviceDebugComponent>(); });
    }
};
static DeviceDebugRegistrar g_deviceDebugRegistrar;
} // namespace

ComponentManifest DeviceDebugComponent::manifest() const
{
    return {QStringLiteral("device.debug"), QStringLiteral("设备调试"), QStringLiteral("1.0"),
            {QStringLiteral("dialog")}};
}

void DeviceDebugComponent::contribute(Registry &registry, const HostContext &)
{
    registry.addDialog(DialogContribution{
        QStringLiteral("dialog.device_debug"),
        [](const QVariantMap &args, QWidget *parent) -> QDialog * {
            const QString sourceId = args.value(QStringLiteral("sourceId")).toString();
            const QString name = args.value(QStringLiteral("name")).toString();
            void *handle = reinterpret_cast<void *>(
                args.value(QStringLiteral("handle")).value<quintptr>());
            const DataInputEntry *source = DataInputRegistry::instance().findSourceById(sourceId);
            if (!source || !source->send)
                return nullptr;
            return new DeviceDebugDialog(
                *source, name,
                [source, handle] { return handle ? source->status(handle) : QString(); },
                [source, handle](const QString &cmd) {
                    return handle ? source->send(handle, cmd)
                                  : QStringLiteral("{\"ok\":false,\"error\":\"未连接\"}");
                },
                parent);
        }});
}

} // namespace ps::components
