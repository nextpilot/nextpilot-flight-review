#include "DynamicExtensionRegistry.h"

#include "Component.h"
#include "DynamicExtension.h"
#include "ExtensionRuntime.h"
#include "ps_extension_api.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace ps::components
{

QStringList registerDynamicExtensions()
{
    QStringList errors;
    for (const LoadedExtension &loaded : ExtensionRuntime::instance().loadedExtensions())
    {
        if (!loaded.descriptor)
            continue;
        const PsExtensionDescriptor *d = loaded.descriptor;
        const QJsonObject manifest =
            QJsonDocument::fromJson(QByteArray(d->manifest_json ? d->manifest_json : "")).object();
        QString id;
        if (d->loader && d->loader->id)
            id = QString::fromUtf8(d->loader->id);
        else if (d->source && d->source->id)
            id = QString::fromUtf8(d->source->id);
        else
            id = manifest.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
        {
            errors << QStringLiteral("%1：扩展缺少 id").arg(QString::fromStdString(loaded.library));
            continue;
        }
        ExtensionDescriptor descriptor;
        descriptor.id = id;
        descriptor.name = manifest.value(QStringLiteral("name")).toString(id);
        if (descriptor.name.isEmpty() && d->loader && d->loader->label)
            descriptor.name = QString::fromUtf8(d->loader->label);
        if (descriptor.name.isEmpty() && d->source && d->source->label)
            descriptor.name = QString::fromUtf8(d->source->label);
        descriptor.version = manifest.value(QStringLiteral("version")).toString();
        descriptor.type = ExtensionType::Dynamic;
        descriptor.libraryPath = QString::fromStdString(loaded.packageDir);
        descriptor.manifestJson = QString::fromUtf8(d->manifest_json ? d->manifest_json : "");
        descriptor.abiVersion = static_cast<int>(d->abi_version);
        descriptor.descriptor = d;
        if (!ExtensionRegistry::instance().addExtension(
                descriptor, [d] { return std::make_unique<DynamicExtension>(d); }))
            errors << QStringLiteral("%1：扩展 id 冲突 %2").arg(QString::fromStdString(loaded.library), id);
    }
    return errors;
}

} // namespace ps::components
