#pragma once

#include "Selection.h"
#include "Transform.h"
#include "FilterOps.h"
#include "MarkerOps.h"
#include "RegistryNode.h"
#include "RegistryTree.h"

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <functional>

namespace nextpilot::plotscope::context
{

using nextpilot::plotscope::foundation::Result;
using nextpilot::plotscope::foundation::fail;
using nextpilot::plotscope::uischema::Selection;
using nextpilot::plotscope::uischema::SelectionKind;
using nextpilot::plotscope::uischema::TransformScriptOps;
using nextpilot::plotscope::uischema::FilterOps;
using nextpilot::plotscope::uischema::MarkerOps;


// 交互注册表：容纳"注册 + 派发"型扩展点（viewAction / property / transform）。
// 这些是运行时路由行为，不是静态运行态数据，故独立于 AppContext。
// 只用 core 类型，headless、无 QWidget、不含模块依赖。
// 实例归 RuntimeHost 持有；需要取用的模块经 AppContext::interactions() 访问。
class InteractionHub : public nextpilot::plotscope::registry::RegistryNode
{
public:
    // 构造即上树（kind=Interaction，name=交互；M2.8 遗留5：推迟池收口）。
    explicit InteractionHub(nextpilot::plotscope::registry::RegistryTree &tree)
        : RegistryNode(tree, nextpilot::plotscope::registry::RegistryKind::Interaction,
                       QStringLiteral("交互"))
    {
    }

    // ---- RegistryNode 可观测：只暴露 viewAction（其余扩展点是无名能力，不枚举）----
    int count() const override { return m_viewActions.count(); }
    QStringList keysAsStrings() const override { return m_viewActions.keys(); }
    // 视图动作：GUI 视图在工厂里登记部件能力，外壳按 id 触发。
    using ViewActionHandler = std::function<Result<QVariant>(const QVariantMap &params)>;
    void registerViewAction(const QString &id, ViewActionHandler handler);
    bool dispatchViewAction(const QString &id, const QVariantMap &params, Result<QVariant> &out) const;

    // 属性提供者：按 selection.kind 路由，属性面板取 schema 显示。
    using PropertyProvider = std::function<QJsonObject(const Selection &selection)>;
    void registerPropertyProvider(SelectionKind kind, PropertyProvider provider);
    bool queryPropertySchema(const Selection &selection, QJsonObject &out) const;

    // 属性写入器：与 provider 成对注册，按 kind 路由写回真正持有者。
    using PropertySetter = std::function<bool(const Selection &selection, const QString &objectId,
                                              const QString &key, const QString &value, QString *err)>;
    void registerPropertySetter(SelectionKind kind, PropertySetter setter);
    bool applyProperty(const Selection &selection, const QString &objectId,
                       const QString &key, const QString &value, QString *err = nullptr) const;

    // 统一变换求值：实现在 script/Rust，调用方在 host/gui/protocol。
    using TransformEvalFn = std::function<Result<QVector<double>>(
        const QJsonObject &request, uint32_t cap, const QVector<double> &t, const QVector<double> &v)>;
    void registerTransformEvaluator(TransformEvalFn fn);
    bool hasTransformEvaluator() const { return static_cast<bool>(m_transformEvaluator); }
    bool evalTransform(const QJsonObject &request, uint32_t cap,
                       const QVector<double> &t, const QVector<double> &v,
                       Result<QVector<double>> &out) const;

    // 有状态过滤器能力：实现在 script/FilterCatalogue，调用方在 host(data.filter)/gui(过滤器编辑)。
    // 与 transform 求值同理——只用 JSON 回调，不反向依赖 script 模块。
    void registerFilterOps(FilterOps ops);
    bool hasFilterOps() const { return m_filterOps.valid(); }
    const FilterOps &filterOps() const { return m_filterOps; }

    // Marker 规则能力：实现在 script/MarkerEngine，调用方在 host(data.marker)/脚本。
    void registerMarkerOps(MarkerOps ops);
    bool hasMarkerOps() const { return m_markerOps.valid(); }
    const MarkerOps &markerOps() const { return m_markerOps; }

    // Lua 脚本库能力：GUI 保存/解析脚本走这里，不反向依赖 script 模块。
    void registerTransformScriptOps(TransformScriptOps ops);
    bool hasTransformScriptOps() const { return m_transformScriptOps.valid(); }
    const TransformScriptOps &transformScriptOps() const { return m_transformScriptOps; }

    // op 目录：速查面板/自动补全的数据源。存 provider 而非快照——意图 handler
    // 是各模块 initialize 时逐个注册的，快照会漏掉后注册者；调用时实时拉取，
    // 保证目录与实际可执行意图一致。
    using OpCatalogueFn = std::function<QStringList()>;
    void registerOpCatalogue(OpCatalogueFn fn);
    QStringList opCatalogue() const; // 未注册返回空表

private:
    QHash<QString, ViewActionHandler> m_viewActions;
    QHash<int, PropertyProvider> m_propertyProviders;
    QHash<int, PropertySetter> m_propertySetters;
    TransformEvalFn m_transformEvaluator;
    TransformScriptOps m_transformScriptOps;
    FilterOps m_filterOps;
    MarkerOps m_markerOps;
    OpCatalogueFn m_opCatalogue;
};

} // namespace nextpilot::plotscope::context
