#pragma once

#include <QSettings>
#include <QVariant>

namespace ps::commands
{
class CommandManager;
}

class MainWindow;

// 应用通用设置的唯一入口。模块自己的设置页仍可持有业务配置。

class SettingsManager
{
public:
    static SettingsManager &instance();

    bool restoreLayout() const;
    void setRestoreLayout(bool value);

    QVariant value(const QString &key, const QVariant &defaultValue = {}) const;
    void setValue(const QString &key, const QVariant &value);
    void remove(const QString &key);
    void sync();
    void openDialog(MainWindow *parent);
    void registerCommand(ps::commands::CommandManager &commands, MainWindow *parent);

private:
    SettingsManager();
    QSettings m_settings;
};
