#include "SettingsStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

namespace ng::core
{

namespace
{
// 作用域前缀
QString scopePrefix(SettingsScope scope)
{
    switch (scope)
    {
    case SettingsScope::App:       return QStringLiteral("app");
    case SettingsScope::Workspace: return QStringLiteral("ws");
    case SettingsScope::Document:  return QStringLiteral("doc");
    }
    return QStringLiteral("app");
}

SettingsScope scopeOfKey(const QString &key)
{
    if (key.startsWith(QStringLiteral("ws/")))
        return SettingsScope::Workspace;
    if (key.startsWith(QStringLiteral("doc/")))
        return SettingsScope::Document;
    return SettingsScope::App;
}
} // namespace

SettingsStore::SettingsStore(const QString &filePath, QObject *parent)
    : QObject(parent), m_filePath(filePath)
{
    load();
}

QString SettingsStore::scopedKey(SettingsScope scope, const QString &relative) const
{
    if (relative.isEmpty())
        return scopePrefix(scope);
    return scopePrefix(scope) + QLatin1Char('/') + relative;
}

QVariant SettingsStore::value(const QString &key, const QVariant &defaultValue) const
{
    const auto it = m_values.constFind(key);
    return it == m_values.constEnd() ? defaultValue : it.value();
}

void SettingsStore::setValue(const QString &key, const QVariant &value)
{
    if (key.isEmpty())
        return;
    if (m_values.contains(key) && m_values.value(key) == value)
        return;
    m_values.insert(key, value);
    Q_EMIT changed(static_cast<int>(scopeOfKey(key)), key);
}

void SettingsStore::remove(const QString &key)
{
    if (m_values.remove(key) > 0)
        Q_EMIT changed(static_cast<int>(scopeOfKey(key)), key);
}

bool SettingsStore::contains(const QString &key) const
{
    return m_values.contains(key);
}

QStringList SettingsStore::keys() const
{
    QStringList out = m_values.keys();
    out.sort();
    return out;
}

QStringList SettingsStore::keysIn(SettingsScope scope) const
{
    const QString prefix = scopePrefix(scope) + QLatin1Char('/');
    QStringList out;
    for (auto it = m_values.constBegin(); it != m_values.constEnd(); ++it)
        if (it.key().startsWith(prefix))
            out.append(it.key());
    out.sort();
    return out;
}

QVariant SettingsStore::scopedValue(SettingsScope scope, const QString &relative,
                                    const QVariant &defaultValue) const
{
    return value(scopedKey(scope, relative), defaultValue);
}

void SettingsStore::setScopedValue(SettingsScope scope, const QString &relative, const QVariant &value)
{
    setValue(scopedKey(scope, relative), value);
}

void SettingsStore::removeScoped(SettingsScope scope, const QString &relative)
{
    remove(scopedKey(scope, relative));
}

bool SettingsStore::setJson(SettingsScope scope, const QString &relative, const QJsonObject &object)
{
    // 复杂结构统一以 JSON 文本存储：core 不认识具体结构，只负责存取。
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    setScopedValue(scope, relative, QString::fromUtf8(bytes));
    return true;
}

QJsonObject SettingsStore::json(SettingsScope scope, const QString &relative) const
{
    const QVariant v = scopedValue(scope, relative);
    if (v.typeId() == QMetaType::QString)
    {
        const QJsonDocument doc = QJsonDocument::fromJson(v.toString().toUtf8());
        if (doc.isObject())
            return doc.object();
    }
    if (v.canConvert<QJsonObject>())
        return v.toJsonObject();
    return {};
}

void SettingsStore::load()
{
    m_values.clear();
    loadFromDisk();
}

void SettingsStore::loadFromDisk()
{
    if (m_filePath.isEmpty())
        return;
    QFile f(m_filePath);
    if (!f.open(QIODevice::ReadOnly))
        return; // 首次运行：没有文件是正常的
    const QByteArray bytes = f.readAll();
    f.close();

    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject())
        return;
    const QJsonObject root = doc.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it)
    {
        // 值为 JSON 字符串时原样保留（复杂结构的文本），其余按 JSON 值还原
        m_values.insert(it.key(), it.value().toVariant());
    }
}

bool SettingsStore::flush(QString *err)
{
    if (m_filePath.isEmpty())
    {
        Q_EMIT flushed();
        return true; // 内存模式：无事可做
    }
    if (!saveToDisk(err))
        return false;
    Q_EMIT flushed();
    return true;
}

bool SettingsStore::saveToDisk(QString *err) const
{
    // 组装成 JSON 对象：值统一存为 JSON 文本（保证往返稳定）
    QJsonObject root;
    for (auto it = m_values.constBegin(); it != m_values.constEnd(); ++it)
    {
        const QVariant &v = it.value();
        if (v.typeId() == QMetaType::QString)
            root.insert(it.key(), v.toString());
        else
            root.insert(it.key(), QJsonValue::fromVariant(v));
    }

    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);

    // 原子写：QSaveFile 先写临时文件，成功后原子替换
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (err)
            *err = QStringLiteral("无法写入设置文件: %1").arg(m_filePath);
        return false;
    }
    if (file.write(bytes) != bytes.size())
    {
        if (err)
            *err = QStringLiteral("设置文件写入不完整: %1").arg(m_filePath);
        file.cancelWriting();
        return false;
    }
    if (!file.commit())
    {
        if (err)
            *err = QStringLiteral("设置文件提交失败: %1").arg(m_filePath);
        return false;
    }
    return true;
}

} // namespace ng::core
