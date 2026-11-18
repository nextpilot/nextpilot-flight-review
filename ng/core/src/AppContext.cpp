#include "AppContext.h"

namespace ng::core
{

AppContext::AppContext(const QString &settingsPath, QObject *parent)
    : QObject(parent),
      m_settings(settingsPath),
      m_ingest(m_repo) // 摄取管道依赖数据集仓库
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

void AppContext::setActiveChannel(int datasetId, const QString &table, const QString &column)
{
    if (m_activeSourceId == datasetId && m_activeTable == table && m_activeColumn == column)
        return;
    // 源变了才发源变更信号（活动通道变化已带 datasetId，订阅者按需判断）。
    const bool sourceChanged = (m_activeSourceId != datasetId);
    m_activeSourceId = datasetId;
    m_activeTable = table;
    m_activeColumn = column;
    if (sourceChanged)
        Q_EMIT activeSourceChanged(datasetId);
    Q_EMIT activeChannelChanged(datasetId, table, column);
}

void AppContext::clearActiveChannel()
{
    setActiveChannel(-1, QString(), QString());
}

void AppContext::registerViewAction(const QString &id, ViewActionHandler handler)
{
    if (handler)
        m_viewActions.insert(id, std::move(handler));
}

bool AppContext::dispatchViewAction(const QString &id, const QVariantMap &params,
                                    Result<QVariant> &out) const
{
    const auto it = m_viewActions.constFind(id);
    if (it == m_viewActions.constEnd())
        return false;
    out = it.value()(params);
    return true;
}

} // namespace ng::core
