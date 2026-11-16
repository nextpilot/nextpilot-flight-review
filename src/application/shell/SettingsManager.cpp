#include "SettingsManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMetaType>

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

QJsonObject SettingsManager::toJson() const
{
    QJsonObject root;
    for (const QString &key : m_settings.allKeys())
    {
        const QVariant value = m_settings.value(key);
        if (value.metaType().id() == QMetaType::QByteArray)
            root.insert(key, QString::fromLatin1(value.toByteArray().toBase64()));
        else
            root.insert(key, QJsonValue::fromVariant(value));
    }
    return root;
}

bool SettingsManager::exportTo(const QString &path) const
{
    if (path.isEmpty())
        return false;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    return true;
}
