#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QVariant>

#include <functional>

namespace nextpilot::plotscope::gui
{

// 一条可执行命令。id 稳定；handler 由外壳绑定到既有动作/意图路径。
struct CommandDescriptor
{
    QString id;
    QString title;
    QString category;
    QString icon;
    QString when;       // 空 = 总是可用；支持 == != && || 与括号
    QString shortcut;   // 展示用默认键位；实际绑定由 KeybindingRegistry 管理
    QString provider;

    std::function<void()> handler;
    // 动态不可用原因：返回非空即禁用。静态 when 不满足时也禁用。
    std::function<QString()> disabledReason;
};

class CommandRegistry
{
public:
    bool add(const CommandDescriptor &command);
    bool contains(const QString &id) const;
    const CommandDescriptor *find(const QString &id) const;
    QList<CommandDescriptor> commands() const { return m_commands; }
    int count() const { return m_commands.size(); }

    void setContextValue(const QString &key, const QVariant &value);
    QVariant contextValue(const QString &key) const;
    QHash<QString, QVariant> contextValues() const { return m_context; }
    void clearContext();

    // 执行前同时检查 when 与 disabledReason；失败时 error 写原因。
    bool execute(const QString &id, QString *error = nullptr);

    // 最小 when 求值：identifier、true/false、带引号字符串、==、!=、&&、||、括号。
    static bool evaluateWhen(const QString &expression, const QHash<QString, QVariant> &context,
                             bool *ok = nullptr);

private:
    QList<CommandDescriptor> m_commands;
    QHash<QString, QVariant> m_context;
};

} // namespace nextpilot::plotscope::gui
