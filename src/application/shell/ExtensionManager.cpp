#include "ExtensionManager.h"

#include "ExtensionHost.h"
#include "ExtensionRuntime.h"
#include "SettingsManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

namespace
{
QString runtimeError(const std::string &error)
{
    return QString::fromStdString(error);
}
} // namespace

ExtensionManager::ExtensionManager()
    : m_host(std::make_unique<ps::components::ExtensionHost>())
{
}

ExtensionManager::~ExtensionManager() = default;

void ExtensionManager::scanStandardLocations()
{
    QStringList dirs;
    dirs << QCoreApplication::applicationDirPath() + QStringLiteral("/extensions");
    dirs << QCoreApplication::applicationDirPath() + QStringLiteral("/../lib/plotscope/extensions");
    dirs << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/extensions");
    const QString envPath = qEnvironmentVariable("PLOTSCOPE_EXTENSION_PATH");
    if (!envPath.isEmpty())
        dirs << envPath.split(QDir::listSeparator(), Qt::SkipEmptyParts);
    for (const QString &dir : dirs)
        ExtensionRuntime::instance().scanDirectory(dir.toStdString());
}

void ExtensionManager::activate(const ps::components::HostContext &context)
{
    m_host->activate(context, disabledIds());
}

void ExtensionManager::deactivate()
{
    m_host->deactivate();
}

ps::components::Registry &ExtensionManager::registry()
{
    return m_host->registry();
}

const ps::components::Registry &ExtensionManager::registry() const
{
    return m_host->registry();
}

QStringList ExtensionManager::errors() const
{
    QStringList result = m_host->errors();
    for (const auto &error : ExtensionRuntime::instance().errors())
        result << runtimeError(error);
    return result;
}

QVector<ExtensionManager::ExtensionInfo> ExtensionManager::extensions() const
{
    QVector<ExtensionInfo> result;
    const QStringList disabled = disabledIds();
    for (const auto &entry : ps::components::ExtensionRegistry::instance().extensions())
    {
        ExtensionInfo info;
        info.descriptor = entry.descriptor;
        info.active = m_host->activeIds().contains(entry.descriptor.id);
        info.enabled = !disabled.contains(entry.descriptor.id);
        for (const QString &error : m_host->errors())
        {
            if (error.startsWith(entry.descriptor.id + QStringLiteral("：")))
            {
                info.error = error;
                break;
            }
        }
        result.append(info);
    }

    return result;
}

bool ExtensionManager::isActive(const QString &id) const
{
    return m_host->activeIds().contains(id);
}

QStringList ExtensionManager::disabledIds() const
{
    return SettingsManager::instance()
        .value(QStringLiteral("extensions.disabled")).toStringList();
}

bool ExtensionManager::isEnabled(const QString &id) const
{
    return !disabledIds().contains(id);
}

void ExtensionManager::setEnabled(const QString &id, bool enabled)
{
    QStringList disabled = disabledIds();
    if (enabled)
        disabled.removeAll(id);
    else if (!disabled.contains(id))
        disabled.append(id);
    SettingsManager::instance().setValue(QStringLiteral("extensions.disabled"), disabled);
}
