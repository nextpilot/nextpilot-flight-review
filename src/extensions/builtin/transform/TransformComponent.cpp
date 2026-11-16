#include "TransformComponent.h"

#include "TransformDialog.h"

#include <QVariantMap>
#include <QVariantList>

namespace ps::components
{
namespace
{
struct TransformRegistrar
{
    TransformRegistrar()
    {
        ExtensionRegistry::instance().addExtension(
            ExtensionDescriptor{QStringLiteral("data.transform"), QStringLiteral("数据变换"),
                                QStringLiteral("1.0"), ExtensionType::Builtin},
            [] { return std::make_unique<TransformComponent>(); });
    }
};
static TransformRegistrar g_transformRegistrar;
} // namespace

ComponentManifest TransformComponent::manifest() const
{
    return {QStringLiteral("data.transform"), QStringLiteral("数据变换"), QStringLiteral("1.0"),
            {QStringLiteral("dialog")}};
}

void TransformComponent::contribute(Registry &registry, HostContext &)
{
    registry.addDialog(DialogContribution{
        QStringLiteral("dialog.transform"),
        [](const QVariantMap &args, QWidget *parent) -> QDialog * {
            auto *dataset = reinterpret_cast<PsDataset *>(
                args.value(QStringLiteral("dataset")).value<quintptr>());
            QVector<const PsChannelInfo *> channels;
            for (const QVariant &v : args.value(QStringLiteral("channels")).toList())
                channels.append(reinterpret_cast<const PsChannelInfo *>(v.value<quintptr>()));
            return new TransformDialog(dataset, channels,
                                       args.value(QStringLiteral("sourceName")).toString(),
                                       args.value(QStringLiteral("preselect")).toString(), parent);
        }});
}

} // namespace ps::components
