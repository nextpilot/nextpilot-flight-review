#pragma once

// 命令唯一注册表（纯数据 + 求值，无 Widgets）。
//
// 契约（D7 / D8 / D15）：
//   - 命令的权威清单只在这里；Ribbon 组、命令面板、上下文菜单、键位、视图标题栏都只是**挂载点**。
//   - `when` 在装载期编译成不可变表达式树并缓存，运行期只求值、不再解析字符串。
//   - 上下文分 Shell / Panel / View 三层作用域（见 ContextStack），内层覆盖外层。
//   - 解析失败的 when 折成恒假并留 error，调用方把它送进诊断面板，不静默通过。
//
// D7 收口（验收 2）：命令只携带**数据**，不含可执行代码（无 handler）。
// 执行时由壳注入的回调按 localActionId（本地 Ribbon trigger）→ intentOp（意图 op）
// 顺序派发。command 库因此保持纯契约：不认识 IIntentExecutor、不链 intent、无 Widgets。

#include <QHash>
#include <QList>
#include <QString>
#include <QVariant>

#include <functional>
#include <utility>

#include "command/ContextStack.h"
#include "command/WhenExpression.h"
#include "command/WhenKeys.h"

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

    // 执行载荷（纯数据，D7）：壳注入的执行回调按下面顺序派发——
    //   1) localActionId 非空：先试 ShellBinder 本地动作（Ribbon trigger 等）
    //   2) intentOp 非空：构造 Intent(op, data) 交给 IIntentExecutor
    // 两者皆空 = 占位命令（不可执行，add 会拒绝）。
    QString localActionId;
    QString intentOp;
    QString data;        // 意图参数（原样作为 Intent.data 传递）

    std::function<QString()> disabledReason;   // 返回非空即禁用（动态原因）
};

// 命令执行回调：由壳注入，command 库只负责在 execute 时把载荷交出去。
// 返回 false 表示执行失败，error 写原因。
using CommandExecutor = std::function<bool(const CommandDescriptor &command, QString *error)>;

class CommandRegistry
{
public:
    bool add(const CommandDescriptor &command);
    // 移除命令（owner 卸载时整批撤注册项，避免悬空引用）。命中返回 true。
    bool remove(const QString &id);
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

    // 注入执行回调（壳装配期调用一次）。未注入时 execute 返回 false 并说明原因。
    void setExecutor(CommandExecutor executor) { m_executor = std::move(executor); }

    // 执行前同时检查 when 与 disabledReason；失败时 error 写原因。
    bool execute(const QString &id, QString *error = nullptr);

    // 只判可见/可用，不执行。
    bool isAvailable(const QString &id, QString *reason = nullptr) const;

    // 某条命令编译后的 when（未登记时返回恒真表达式）。
    const WhenExpression &whenFor(const QString &id) const;

    // 缓存证据：累计解析次数 / 缓存条目数。相同 when 串只解析一次。
    quint64 whenParseCount() const { return m_parseCount; }
    int whenCacheSize() const { return m_whenCache.size(); }

    // 键约束（N3）：写入非契约表键的次数 + 最近一次未知键。生产应为 0。
    int unknownKeyCount() const { return m_unknownKeys; }
    QString lastUnknownKey() const { return m_lastUnknownKey; }

    // 兼容入口：不经过缓存的一次性求值（仅测试与临时判断使用）。
    static bool evaluateWhen(const QString &expression, const QHash<QString, QVariant> &context,
                             bool *ok = nullptr);

private:
    WhenExpression compileCached(const QString &source, QString *error);
    void checkKey(const QString &key);

    QList<CommandDescriptor> m_commands;
    ContextStack m_context;
    QHash<QString, WhenExpression> m_whenById;
    QHash<QString, WhenExpression> m_whenCache;
    quint64 m_parseCount = 0;
    CommandExecutor m_executor;
    int m_unknownKeys = 0;
    QString m_lastUnknownKey;
};

} // namespace nextpilot::plotscope::command
