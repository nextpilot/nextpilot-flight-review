#include "AppContext.h"

namespace nextpilot::plotscope::context
{


AppContext::AppContext(const QString &settingsPath, QObject *parent)
    : QObject(parent),
      m_registryTree(),             // 组合根唯一持有树（成员逆序析构：节点先亡、向仍活着的树摘除）
      m_repo(m_registryTree),       // M2.8：数据集仓库上树
      m_settings(settingsPath),
      m_toolbox(m_registryTree),    // 唯一扩展注册表：内置包 + contributes 贡献面
      m_sources(m_registryTree),    // M2.8 遗留5 收口：数据源上树
      m_ingest(m_repo),             // 摄取管道依赖数据集仓库
      m_interactions(m_registryTree),        // M2.8 遗留5 收口：交互注册表上树
      m_derivedHub(m_registryTree)      // M2.8 遗留5 收口：派生序列上树
{
}

AppContext::~AppContext() = default;

void AppContext::setActiveSourceId(int id)
{
    if (m_activeSourceId == id)
        return;
    m_activeSourceId = id;
    Q_EMIT activeSourceChanged(id);
}

void AppContext::setSourceTimeOffset(int sourceId, double seconds)
{
    // 值不变不广播（画布刷新有成本）。
    if (qFuzzyCompare(m_sourceTimeOffsets.value(sourceId, 0.0), seconds))
        return;
    m_sourceTimeOffsets.insert(sourceId, seconds);
    Q_EMIT sourceTimeOffsetChanged(sourceId, seconds);
}

void AppContext::setActiveChannel(int datasetId, const QString &table, const QString &column)
{
    if (m_activeDatasetId == datasetId && m_activeTable == table &&
        m_activeColumn == column)
        return;
    // 只改活动**数据集**——活动源 id 由调用方显式给出（源与数据集不是一回事）。
    m_activeDatasetId = datasetId;
    m_activeTable = table;
    m_activeColumn = column;
    Q_EMIT activeChannelChanged(datasetId, table, column);
}

void AppContext::clearActiveChannel()
{
    setActiveChannel(-1, QString(), QString());
}

void AppContext::setSelection(const Selection &selection)
{
    if (selection.key() == m_selection.key())
        return;
    m_selection = selection;
    Q_EMIT selectionChanged(selection.toJson());
}

void AppContext::clearSelection()
{
    if (m_selection.empty())
        return;
    m_selection.clear();
    Q_EMIT selectionChanged(m_selection.toJson());
}

void AppContext::setCommandHooks(
    std::function<bool(const QString &, QString *)> invoke,
    std::function<bool(const QString &, const QHash<QString, QVariant> &)> when)
{
    m_invokeCommand = std::move(invoke);
    m_evalWhen = std::move(when);
}

bool AppContext::invokeCommand(const QString &commandId, QString *error) const
{
    if (!m_invokeCommand)
    {
        if (error)
            *error = QStringLiteral("命令层未接入（headless 或外壳未完成装配）");
        return false;
    }
    return m_invokeCommand(commandId, error);
}

bool AppContext::evaluateWhen(const QString &when,
                              const QHash<QString, QVariant> &extra) const
{
    if (when.isEmpty())
        return true; // 空表达式恒真：声明不带 when 就是"总是出现"
    if (!m_evalWhen)
        return false; // 没有求值器时不要把声明当可见，避免出现"点了没事发生"的菜单项
    return m_evalWhen(when, extra);
}

void AppContext::appendMessage(const QString &level, const QString &source, const QString &text,
                               const QString &locator)
{
    // 入库与通知绑定在一起：面板只订阅信号，谁 append 都走同一条路，
    // 不存在"写了日志但没通知面板"的漏项。
    m_messages.append(level, source, text, locator);
    Q_EMIT messageAppended(level, source, text, locator);
}

} // namespace nextpilot::plotscope::context
