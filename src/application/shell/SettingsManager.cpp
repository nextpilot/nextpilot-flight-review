#include "SettingsManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMetaType>
#include <QStringList>

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

QString SettingsManager::toYaml() const
{
    QString out = QStringLiteral("# PlotScope settings\n");
    for (const QString &key : m_settings.allKeys())
    {
        const QVariant value = m_settings.value(key);
        QString text;
        if (value.metaType().id() == QMetaType::QByteArray)
            text = QStringLiteral("\"%1\"").arg(QString::fromLatin1(value.toByteArray().toBase64()));
        else if (value.metaType().id() == QMetaType::QStringList)
        {
            QStringList parts;
            for (const QString &item : value.toStringList())
                parts << QStringLiteral("\"%1\"").arg(QString(item).replace(QLatin1Char('"'), QStringLiteral("\\\"")));
            text = QStringLiteral("[%1]").arg(parts.join(QStringLiteral(", ")));
        }
        else if (value.metaType().id() == QMetaType::Bool)
            text = value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
        else if (value.metaType().id() == QMetaType::Int || value.metaType().id() == QMetaType::LongLong ||
                 value.metaType().id() == QMetaType::UInt || value.metaType().id() == QMetaType::ULongLong)
            text = value.toString();
        else if (value.metaType().id() == QMetaType::Double || value.metaType().id() == QMetaType::Float)
            text = QString::number(value.toDouble(), 'g', 12);
        else
            text = QStringLiteral("\"%1\"").arg(QString(value.toString()).replace(QLatin1Char('"'), QStringLiteral("\\\"")));
        out += QStringLiteral("%1: %2\n").arg(key, text);
    }
    return out;
}

bool SettingsManager::exportTo(const QString &path) const
{
    if (path.isEmpty())
        return false;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("yaml") || suffix == QLatin1String("yml"))
        file.write(toYaml().toUtf8());
    else
        file.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    return true;
}
