#include "AppContext.h"

namespace nextpilot::plotscope::core
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

} // namespace nextpilot::plotscope::core
