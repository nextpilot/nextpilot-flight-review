#include "SettingsStore.h"

namespace ps::runtime
{

SettingsStore::SettingsStore(const QString &organization, const QString &application)
    : m_settings(QSettings::defaultFormat(), QSettings::UserScope, organization, application)
{
}

QVariant SettingsStore::value(const QString &key, const QVariant &defaultValue) const
{
    return m_settings.value(key, defaultValue);
}

void SettingsStore::setValue(const QString &key, const QVariant &value)
{
    m_settings.setValue(key, value);
}

void SettingsStore::remove(const QString &key)
{
    m_settings.remove(key);
}

void SettingsStore::sync()
{
    m_settings.sync();
}

QStringList SettingsStore::allKeys() const
{
    return m_settings.allKeys();
}

QString SettingsStore::fileName() const
{
    return m_settings.fileName();
}

QSettings::Status SettingsStore::status() const
{
    return m_settings.status();
}

} // namespace ps::runtime
