#pragma once

// 命令唯一注册表（纯数据 + 求值，无 Widgets）。
//
// 契约（D7 / D8 / D15）：
//   - 命令的权威清单只在这里；Ribbon 组、命令面板、上下文菜单、键位、视图标题栏都只是**挂载点**。
//   - `when` 在装载期编译成不可变表达式树并缓存，运行期只求值、不再解析字符串。
//   - 上下文分 Shell / Panel / View 三层作用域（见 ContextStack），内层覆盖外层。
//   - 解析失败的 when 折成恒假并留 error，调用方把它送进诊断面板，不静默通过。
//
// 过渡说明：`handler` 仍在，供壳内既有动作使用；D7 的 `intentOp` 收口在下一步提交里做，
// 最终形态是"命令只携带 op，执行走 IIntentExecutor"，届时 `handler` 被删除。

#include <QHash>
#include <QList>
#include <QString>
#include <QVariant>

#include <functional>

#include "command/ContextStack.h"
#include "command/WhenExpression.h"

namespace nextpilot::plotscope::command
{

struct CommandDescriptor
{
    QString id;
    QString title;
    QString category;
    QString icon;
    QString when;        // 空 = 总是可用；语法见 WhenExpression.h
    QString shortcut;    // 展示用默认键位；实际绑定由 KeybindingRegistry 管理
    QString provider;

    std::function<void()> handler;
    std::function<QString()> disabledReason;   // 返回非空即禁用（动态原因）
};

class CommandRegistry
{
public:
    bool add(const CommandDescriptor &command);
    bool contains(const QString &id) const;
    const CommandDescriptor *find(const QString &id) const;
    QList<CommandDescriptor> commands() const { return m_commands; }
    int count() const { return m_commands.size(); }

    // 上下文：默认写 Shell 层；需要分层时显式给 scope。
    void setContextValue(const QString &key, const QVariant &value);
    void setContextValue(ContextScope scope, const QString &key, const QVariant &value);
    QVariant contextValue(const QString &key) const;
    QHash<QString, QVariant> contextValues() const { return m_context.merged(); }
    const ContextStack &context() const { return m_context; }
    void clearContext();

    // 执行前同时检查 when 与 disabledReason；失败时 error 写原因。
    bool execute(const QString &id, QString *error = nullptr);

    // 只判可见/可用，不执行。
    bool isAvailable(const QString &id, QString *reason = nullptr) const;

    // 某条命令编译后的 when（未登记时返回恒真表达式）。
    const WhenExpression &whenFor(const QString &id) const;

    // 缓存证据：累计解析次数 / 缓存条目数。相同 when 串只解析一次。
    quint64 whenParseCount() const { return m_parseCount; }
    int whenCacheSize() const { return m_whenCache.size(); }

    // 兼容入口：不经过缓存的一次性求值（仅测试与临时判断使用）。
    static bool evaluateWhen(const QString &expression, const QHash<QString, QVariant> &context,
                             bool *ok = nullptr);

private:
    WhenExpression compileCached(const QString &source, QString *error);

    QList<CommandDescriptor> m_commands;
    ContextStack m_context;
    QHash<QString, WhenExpression> m_whenById;
    QHash<QString, WhenExpression> m_whenCache;
    quint64 m_parseCount = 0;
};

} // namespace nextpilot::plotscope::command
