#include "SettingsManager.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMetaType>
#include <QStringList>
#include <QTimer>

SettingsManager &SettingsManager::instance()
{
    static SettingsManager manager;
    return manager;
}

SettingsManager::SettingsManager()
    : m_settings(QStringLiteral("NextPilot"), QStringLiteral("PlotScope"))
{
    snapshot();
    setupWatchers();
}

bool SettingsManager::restoreLayout() const
{
    return get(ps::settings::kRestoreLayout);
}

void SettingsManager::setRestoreLayout(bool value)
{
    set(ps::settings::kRestoreLayout, value);
}

QVariant SettingsManager::value(const QString &key, const QVariant &defaultValue) const
{
    return m_settings.value(key, defaultValue);
}

void SettingsManager::setValue(const QString &key, const QVariant &value)
{
    const QVariant oldValue = m_settings.value(key);
    if (oldValue.isValid() && oldValue == value)
        return;
    m_settings.setValue(key, value);
    if (m_settings.status() != QSettings::NoError)
        qWarning("settings: write failed for '%s', status=%d", qPrintable(key), int(m_settings.status()));
    m_snapshot.insert(key, value);
    emitIfChanged(key, oldValue, value);
}

void SettingsManager::remove(const QString &key)
{
    const QVariant oldValue = m_settings.value(key);
    if (!oldValue.isValid())
        return;
    m_settings.remove(key);
    if (m_settings.status() != QSettings::NoError)
        qWarning("settings: remove failed for '%s', status=%d", qPrintable(key), int(m_settings.status()));
    m_snapshot.remove(key);
    emitIfChanged(key, oldValue, QVariant());
}

void SettingsManager::sync()
{
    syncFromDisk();
}

void SettingsManager::snapshot()
{
    m_snapshot.clear();
    for (const QString &key : m_settings.allKeys())
        m_snapshot.insert(key, m_settings.value(key));
}

void SettingsManager::syncFromDisk()
{
    if (m_syncing)
        return;
    m_syncing = true;
    m_settings.sync();
    if (m_settings.status() != QSettings::NoError)
        qWarning("settings: sync failed, status=%d", int(m_settings.status()));

    QMap<QString, QVariant> next;
    for (const QString &key : m_settings.allKeys())
        next.insert(key, m_settings.value(key));

    for (auto it = m_snapshot.cbegin(); it != m_snapshot.cend(); ++it)
    {
        if (!next.contains(it.key()))
            emitIfChanged(it.key(), it.value(), QVariant());
    }
    for (auto it = next.cbegin(); it != next.cend(); ++it)
    {
        const QVariant old = m_snapshot.value(it.key());
        emitIfChanged(it.key(), old, it.value());
    }
    m_snapshot = next;
    m_syncing = false;
}

void SettingsManager::setupWatchers()
{
    const QString fileName = m_settings.fileName();
    // Windows 注册表后端 fileName() 不是绝对路径；只有文件后端才挂 watcher。
    if (!fileName.isEmpty() && QDir::isAbsolutePath(fileName))
    {
        m_watcher = new QFileSystemWatcher(this);
        m_debounce = new QTimer(this);
        m_debounce->setSingleShot(true);
        m_debounce->setInterval(250);
        connect(m_debounce, &QTimer::timeout, this, &SettingsManager::syncFromDisk);
        const QString dir = QFileInfo(fileName).absolutePath();
        QDir().mkpath(dir);
        if (QFileInfo::exists(fileName))
            m_watcher->addPath(fileName);
        if (QFileInfo::exists(dir))
            m_watcher->addPath(dir);

        connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this, fileName](const QString &) {
            if (QFileInfo::exists(fileName) && !m_watcher->files().contains(fileName))
                m_watcher->addPath(fileName);
            m_debounce->start();
        });
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this, fileName](const QString &) {
            if (QFileInfo::exists(fileName) && !m_watcher->files().contains(fileName))
                m_watcher->addPath(fileName);
            m_debounce->start();
        });
        return;
    }

    // Windows 注册表或其他非文件后端没有文件可监听，低频轮询兜底。
    m_poll = new QTimer(this);
    m_poll->setInterval(3000);
    connect(m_poll, &QTimer::timeout, this, &SettingsManager::syncFromDisk);
    m_poll->start();
}

void SettingsManager::emitIfChanged(const QString &key, const QVariant &oldValue, const QVariant &newValue)
{
    if (oldValue == newValue)
        return;
    emit changed(key, oldValue, newValue);
}

QMetaObject::Connection SettingsManager::onChanged(
    const QString &key, QObject *context,
    std::function<void(const QVariant &oldValue, const QVariant &newValue)> handler)
{
    if (!handler)
        return {};
    QObject *receiver = context ? context : this;
    return connect(this, &SettingsManager::changed, receiver,
                   [key, handler = std::move(handler)](const QString &changedKey, const QVariant &oldValue,
                                                       const QVariant &newValue) {
                       if (changedKey == key)
                           handler(oldValue, newValue);
                   });
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
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QString suffix = QFileInfo(path).suffix().toLower();
    const QByteArray data = (suffix == QLatin1String("yaml") || suffix == QLatin1String("yml"))
                                ? toYaml().toUtf8()
                                : QJsonDocument(toJson()).toJson(QJsonDocument::Indented);
    const qint64 written = file.write(data);
    if (written != data.size() || !file.flush())
        return false;
    file.close();
    return file.error() == QFileDevice::NoError;
}
