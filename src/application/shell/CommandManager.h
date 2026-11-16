#pragma once

#include <QString>
#include <QStringList>

#include <functional>
#include <map>

class QWidget;

namespace ps::commands
{

struct CommandContext
{
    QWidget *parent = nullptr;
    QString arg;
};

struct CommandSpec
{
    QString id;
    QString text;
    QString icon;
    QString shortcut;
    bool checkable = false;
    std::function<bool(const CommandContext &)> enabled;
    std::function<bool(const CommandContext &)> checked;
    bool sessionDirty = false; // 命令成功后是否需要保存会话/布局
};

class CommandManager
{
public:
    using Handler = std::function<void(const CommandContext &)>;

    void add(const QString &id, Handler handler);
    void describe(const CommandSpec &spec);
    void registerCommand(const CommandSpec &spec, Handler handler);
    void bindShortcuts(QWidget *owner);
    bool contains(const QString &id) const;
    bool invoke(const QString &id, const CommandContext &ctx) const;
    bool isEnabled(const QString &id, const CommandContext &ctx) const;
    bool isChecked(const QString &id, const CommandContext &ctx) const;
    const CommandSpec *spec(const QString &id) const;
    const std::map<QString, CommandSpec> &specs() const { return m_specs; }
    void setInvokeHook(std::function<void(const QString &)> hook) { m_invokeHook = std::move(hook); }

private:
    std::map<QString, Handler> m_handlers;
    std::map<QString, CommandSpec> m_specs;
    std::function<void(const QString &)> m_invokeHook;
};

} // namespace ps::commands
