#pragma once

#include <QDebug>
#include <QObject>
#include <QMetaType>
#include <QSettings>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QVariant>
#include <QMap>
#include <functional>
#include <type_traits>

class QFileSystemWatcher;
class QTimer;
class MainWindow;

namespace ps::commands
{
class CommandManager;
}

namespace ps::settings
{

template <typename T>
struct SettingsKey
{
    QString name;
    T defaultValue;
};

// 应用设置 key 只在这里定义；业务代码不再散写字符串。
inline const SettingsKey<bool> kRestoreLayout{QStringLiteral("restoreLayout"), false};
inline const SettingsKey<QString> kTheme{QStringLiteral("theme"), QStringLiteral("dark")};
inline const SettingsKey<QString> kLastLayout{QStringLiteral("lastLayout"), QString()};
inline const SettingsKey<QStringList> kRecentFiles{QStringLiteral("recentFiles"), {}};
inline const SettingsKey<QStringList> kAnalysisRecent{QStringLiteral("analysis.recent"), {}};
inline const SettingsKey<QStringList> kAnalysisFavorites{QStringLiteral("analysis.favorites"), {}};
inline const SettingsKey<QByteArray> kWindowGeometry{QStringLiteral("windowGeometry"), QByteArray()};
inline const SettingsKey<QByteArray> kWindowState{QStringLiteral("windowState"), QByteArray()};
inline const SettingsKey<QStringList> kExtensionsDisabled{QStringLiteral("extensions.disabled"), {}};
inline const SettingsKey<int> kDerivedMaxPoints{QStringLiteral("derivedMaxPoints"), 2000000};
inline const SettingsKey<int> kExportSampleLimit{QStringLiteral("exportSampleLimit"), 0};
inline const SettingsKey<bool> kGridDefault{QStringLiteral("gridDefault"), true};
inline const SettingsKey<bool> kLegendDefault{QStringLiteral("legendDefault"), true};
inline const SettingsKey<QString> kTimeFormat{QStringLiteral("timeFormat"), QStringLiteral("relative")};
// 上次会话的数据源配置（JSON 数组）；只做持久化，不在启动时自动连接。
inline const SettingsKey<QString> kDataSourceConfigs{QStringLiteral("dataSource.configs"), QStringLiteral("[]")};

} // namespace ps::settings

// 应用设置的唯一入口：可观察、强类型、可枚举，同时保留原始 QVariant 接口给扩展。
class SettingsManager : public QObject
{
    Q_OBJECT

public:
    static SettingsManager &instance();

    bool restoreLayout() const;
    void setRestoreLayout(bool value);

    template <typename T>
    T get(const ps::settings::SettingsKey<T> &key) const
    {
        const QVariant raw = m_settings.value(key.name, QVariant::fromValue(key.defaultValue));
        T out{};
        QString error;
        if (!decode(raw, key.defaultValue, &out, &error))
        {
            qWarning("settings: key '%s' expected %s, got %s (%s)",
                     qPrintable(key.name), typeName<T>(),
                     qPrintable(raw.metaType().name()), qPrintable(error));
            return key.defaultValue;
        }
        return out;
    }

    template <typename T>
    void set(const ps::settings::SettingsKey<T> &key, const T &value)
    {
        setValue(key.name, QVariant::fromValue(value));
    }

    template <typename T>
    void remove(const ps::settings::SettingsKey<T> &key)
    {
        remove(key.name);
    }

    QVariant value(const QString &key, const QVariant &defaultValue = {}) const;
    void setValue(const QString &key, const QVariant &value);
    void remove(const QString &key);
    void sync();
    QJsonObject toJson() const;
    QString toYaml() const;
    bool exportTo(const QString &path) const;
    void openDialog(MainWindow *parent);
    void registerCommand(ps::commands::CommandManager &commands, MainWindow *parent);

    QMetaObject::Connection onChanged(const QString &key, QObject *context,
                                      std::function<void(const QVariant &oldValue, const QVariant &newValue)> handler);

    template <typename T, typename Handler>
    QMetaObject::Connection onChanged(const ps::settings::SettingsKey<T> &key, QObject *context, Handler handler)
    {
        std::function<void(const T &)> fn = std::move(handler);
        return onChanged(key.name, context, [key, fn](const QVariant &, const QVariant &next) {
            T value{};
            if (decode(next, key.defaultValue, &value, nullptr) && fn)
                fn(value);
        });
    }

signals:
    void changed(const QString &key, const QVariant &oldValue, const QVariant &newValue);

private:
    SettingsManager();
    void snapshot();
    void syncFromDisk();
    void setupWatchers();
    void emitIfChanged(const QString &key, const QVariant &oldValue, const QVariant &newValue);

    template <typename T>
    static const char *typeName()
    {
        if constexpr (std::is_same_v<T, bool>)
            return "bool";
        if constexpr (std::is_same_v<T, int>)
            return "int";
        if constexpr (std::is_same_v<T, QString>)
            return "QString";
        if constexpr (std::is_same_v<T, QStringList>)
            return "QStringList";
        if constexpr (std::is_same_v<T, QByteArray>)
            return "QByteArray";
        return "value";
    }

    template <typename T>
    static bool decode(const QVariant &raw, const T &, T *out, QString *error)
    {
        if constexpr (std::is_same_v<T, bool>)
        {
            if (raw.metaType().id() == QMetaType::Bool)
            {
                *out = raw.toBool();
                return true;
            }
            // 兼容旧配置里的 "true"/"false" 字符串。
            if (raw.metaType().id() == QMetaType::QString)
            {
                const QString text = raw.toString().trimmed().toLower();
                if (text == QLatin1String("true") || text == QLatin1String("false"))
                {
                    *out = text == QLatin1String("true");
                    return true;
                }
            }
            if (error)
                *error = QStringLiteral("not a bool");
            return false;
        }
        else if constexpr (std::is_same_v<T, int>)
        {
            const int id = raw.metaType().id();
            if (id != QMetaType::Int && id != QMetaType::UInt && id != QMetaType::LongLong &&
                id != QMetaType::ULongLong && id != QMetaType::Short && id != QMetaType::UShort)
            {
                if (error)
                    *error = QStringLiteral("not an integer");
                return false;
            }
            bool ok = false;
            const qlonglong v = raw.toLongLong(&ok);
            if (!ok)
            {
                if (error)
                    *error = QStringLiteral("integer conversion failed");
                return false;
            }
            *out = static_cast<T>(v);
            return true;
        }
        else if constexpr (std::is_same_v<T, QString>)
        {
            if (raw.metaType().id() != QMetaType::QString)
            {
                if (error)
                    *error = QStringLiteral("not a string");
                return false;
            }
            *out = raw.toString();
            return true;
        }
        else if constexpr (std::is_same_v<T, QStringList>)
        {
            if (raw.metaType().id() != QMetaType::QStringList)
            {
                if (error)
                    *error = QStringLiteral("not a string list");
                return false;
            }
            *out = raw.toStringList();
            return true;
        }
        else if constexpr (std::is_same_v<T, QByteArray>)
        {
            if (raw.metaType().id() != QMetaType::QByteArray)
            {
                if (error)
                    *error = QStringLiteral("not a byte array");
                return false;
            }
            *out = raw.toByteArray();
            return true;
        }
        else
        {
            if (raw.metaType().id() != QMetaType::fromType<T>().id())
            {
                if (error)
                    *error = QStringLiteral("type mismatch");
                return false;
            }
            *out = raw.value<T>();
            return true;
        }
    }

    QSettings m_settings;
    QMap<QString, QVariant> m_snapshot;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_debounce = nullptr;
    QTimer *m_poll = nullptr;
    bool m_syncing = false;
};
