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

void AppContext::registerPropertyProvider(SelectionKind kind, PropertyProvider provider)
{
    if (provider)
        m_propertyProviders.insert(static_cast<int>(kind), std::move(provider));
}

bool AppContext::queryPropertySchema(const Selection &selection, QJsonObject &out) const
{
    const auto it = m_propertyProviders.constFind(static_cast<int>(selection.kind));
    if (it == m_propertyProviders.constEnd())
        return false;
    out = it.value()(selection);
    return true;
}

void AppContext::registerPropertySetter(SelectionKind kind, PropertySetter setter)
{
    if (setter)
        m_propertySetters.insert(static_cast<int>(kind), std::move(setter));
}

bool AppContext::applyProperty(const Selection &selection, const QString &objectId,
                               const QString &key, const QString &value, QString *err) const
{
    const auto it = m_propertySetters.constFind(static_cast<int>(selection.kind));
    if (it == m_propertySetters.constEnd())
    {
        if (err)
            *err = QStringLiteral("该对象类型没有属性写入器");
        return false;
    }
    return it.value()(selection, objectId, key, value, err);
}

void AppContext::registerTransformEvaluator(TransformEvalFn fn)
{
    m_transformEvaluator = std::move(fn);
}

bool AppContext::evalTransform(const QJsonObject &request, uint32_t cap,
                               const QVector<double> &t, const QVector<double> &v,
                               Result<QVector<double>> &out) const
{
    if (!m_transformEvaluator)
        return false;
    out = m_transformEvaluator(request, cap, t, v);
    return true;
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

} // namespace nextpilot::plotscope::core
