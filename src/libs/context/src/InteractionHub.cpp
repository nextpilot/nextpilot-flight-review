#include "InteractionHub.h"

namespace nextpilot::plotscope::context
{


void InteractionHub::registerViewAction(const QString &id, ViewActionHandler handler)
{
    if (handler)
    {
        m_viewActions.insert(id, std::move(handler));
        Q_EMIT objectAdded(id); // M2.8：统一节点信号（带 key）
    }
}

bool InteractionHub::dispatchViewAction(const QString &id, const QVariantMap &params,
                                             Result<QVariant> &out) const
{
    const auto it = m_viewActions.constFind(id);
    if (it == m_viewActions.constEnd())
        return false;
    // 先拷贝再调用：handler 执行期间可能注册新 action（面板构造期就 insert），
    // 重散列会把被引用的函数对象搬走并释放，引用调用会踩空。
    const ViewActionHandler handler = it.value();
    out = handler(params);
    return true;
}

void InteractionHub::registerPropertyProvider(SelectionKind kind, PropertyProvider provider)
{
    if (provider)
        m_propertyProviders.insert(static_cast<int>(kind), std::move(provider));
}

bool InteractionHub::queryPropertySchema(const Selection &selection, QJsonObject &out) const
{
    const auto it = m_propertyProviders.constFind(static_cast<int>(selection.kind));
    if (it == m_propertyProviders.constEnd())
        return false;
    out = it.value()(selection);
    return true;
}

void InteractionHub::registerPropertySetter(SelectionKind kind, PropertySetter setter)
{
    if (setter)
        m_propertySetters.insert(static_cast<int>(kind), std::move(setter));
}

bool InteractionHub::applyProperty(const Selection &selection, const QString &objectId,
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

void InteractionHub::registerTransformEvaluator(TransformEvalFn fn)
{
    m_transformEvaluator = std::move(fn);
}

bool InteractionHub::evalTransform(const QJsonObject &request, uint32_t cap,
                                        const QVector<double> &t, const QVector<double> &v,
                                        Result<QVector<double>> &out) const
{
    if (!m_transformEvaluator)
        return false;
    out = m_transformEvaluator(request, cap, t, v);
    return true;
}

void InteractionHub::registerFilterOps(FilterOps ops)
{
    m_filterOps = std::move(ops);
}

void InteractionHub::registerMarkerOps(MarkerOps ops)
{
    m_markerOps = std::move(ops);
}

void InteractionHub::registerTransformScriptOps(TransformScriptOps ops)
{
    m_transformScriptOps = std::move(ops);
}

void InteractionHub::registerOpCatalogue(OpCatalogueFn fn)
{
    m_opCatalogue = std::move(fn);
}

QStringList InteractionHub::opCatalogue() const
{
    return m_opCatalogue ? m_opCatalogue() : QStringList();
}

} // namespace nextpilot::plotscope::context
