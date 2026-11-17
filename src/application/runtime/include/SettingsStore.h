#pragma once

#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace ps::runtime
{

// 应用配置持久化。只做键值读写，不关心业务 key，也不依赖 Widgets。
// shell 的 SettingsManager 在其上增加强类型、变更通知和导出能力。
class SettingsStore
{
public:
    SettingsStore(const QString &organization, const QString &application);

    QVariant value(const QString &key, const QVariant &defaultValue = {}) const;
    void setValue(const QString &key, const QVariant &value);
    void remove(const QString &key);
    void sync();

    QStringList allKeys() const;
    QString fileName() const;
    QSettings::Status status() const;

private:
    QSettings m_settings;
};

} // namespace ps::runtime
