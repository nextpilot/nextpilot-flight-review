#include "ExtensionHost.h"

#include "DynamicExtension.h"
#include "ExtensionRuntime.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace ps::components
{

ExtensionHost::~ExtensionHost()
{
    deactivate();
}

void ExtensionHost::activate(const HostContext &context)
{
    if (!m_components.empty())
        return;
    m_errors.clear();
    for (const ExtensionEntry &entry : ExtensionRegistry::instance().extensions())
    {
        std::unique_ptr<IComponent> component = entry.factory();
        if (!component)
        {
            m_errors << QStringLiteral("%1：组件工厂返回空").arg(entry.descriptor.id);
            continue;
        }
        component->contribute(m_registry, context);
        m_components.push_back(std::move(component));
    }
}

void ExtensionHost::deactivate()
{
    m_components.clear();
}

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
