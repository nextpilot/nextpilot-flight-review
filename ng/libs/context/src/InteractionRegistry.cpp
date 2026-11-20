#include "InteractionRegistry.h"

namespace nextpilot::plotscope::context
{


void InteractionRegistry::registerViewAction(const QString &id, ViewActionHandler handler)
{
    if (handler)
        m_viewActions.insert(id, std::move(handler));
}

bool InteractionRegistry::dispatchViewAction(const QString &id, const QVariantMap &params,
                                             Result<QVariant> &out) const
{
    const auto it = m_viewActions.constFind(id);
    if (it == m_viewActions.constEnd())
        return false;
    out = it.value()(params);
    return true;
}

void InteractionRegistry::registerPropertyProvider(SelectionKind kind, PropertyProvider provider)
{
    if (provider)
        m_propertyProviders.insert(static_cast<int>(kind), std::move(provider));
}

bool InteractionRegistry::queryPropertySchema(const Selection &selection, QJsonObject &out) const
{
    const auto it = m_propertyProviders.constFind(static_cast<int>(selection.kind));
    if (it == m_propertyProviders.constEnd())
        return false;
    out = it.value()(selection);
    return true;
}

void InteractionRegistry::registerPropertySetter(SelectionKind kind, PropertySetter setter)
{
    if (setter)
        m_propertySetters.insert(static_cast<int>(kind), std::move(setter));
}

bool InteractionRegistry::applyProperty(const Selection &selection, const QString &objectId,
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

void InteractionRegistry::registerTransformEvaluator(TransformEvalFn fn)
{
    m_transformEvaluator = std::move(fn);
}

bool InteractionRegistry::evalTransform(const QJsonObject &request, uint32_t cap,
                                        const QVector<double> &t, const QVector<double> &v,
                                        Result<QVector<double>> &out) const
{
    if (!m_transformEvaluator)
        return false;
    out = m_transformEvaluator(request, cap, t, v);
    return true;
}

void InteractionRegistry::registerFilterOps(FilterOps ops)
{
    m_filterOps = std::move(ops);
}

void InteractionRegistry::registerMarkerOps(MarkerOps ops)
{
    m_markerOps = std::move(ops);
}

void InteractionRegistry::registerTransformScriptOps(TransformScriptOps ops)
{
    m_transformScriptOps = std::move(ops);
}

} // namespace nextpilot::plotscope::context
