#include "SettingsManager.h"

SettingsManager &SettingsManager::instance()
{
    static SettingsManager manager;
    return manager;
}

SettingsManager::SettingsManager()
    : m_settings(QStringLiteral("NextPilot"), QStringLiteral("PlotScope"))
{
}

bool SettingsManager::restoreLayout() const
{
    return m_settings.value(QStringLiteral("restoreLayout"), false).toBool();
}

void SettingsManager::setRestoreLayout(bool value)
{
    m_settings.setValue(QStringLiteral("restoreLayout"), value);
}

QVariant SettingsManager::value(const QString &key, const QVariant &defaultValue) const
{
    return m_settings.value(key, defaultValue);
}

void SettingsManager::setValue(const QString &key, const QVariant &value)
{
    m_settings.setValue(key, value);
}

void SettingsManager::remove(const QString &key)
{
    m_settings.remove(key);
}

void SettingsManager::sync()
{
    m_settings.sync();
}
