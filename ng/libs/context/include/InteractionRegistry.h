#pragma once

#include "Selection.h"
#include "Transform.h"

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


// 交互注册表：容纳"注册 + 派发"型扩展点（viewAction / property / transform）。
// 这些是运行时路由行为，不是静态运行态数据，故独立于 AppContext。
// 只用 core 类型，headless、无 QWidget、不含模块依赖。
// 实例归 RuntimeHost 持有；需要取用的器官经 AppContext::interactions() 访问。
class InteractionRegistry
{
public:
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

    // Lua 脚本库能力：GUI 保存/解析脚本走这里，不反向依赖 script 模块。
    void registerTransformScriptOps(TransformScriptOps ops);
    bool hasTransformScriptOps() const { return m_transformScriptOps.valid(); }
    const TransformScriptOps &transformScriptOps() const { return m_transformScriptOps; }

private:
    QHash<QString, ViewActionHandler> m_viewActions;
    QHash<int, PropertyProvider> m_propertyProviders;
    QHash<int, PropertySetter> m_propertySetters;
    TransformEvalFn m_transformEvaluator;
    TransformScriptOps m_transformScriptOps;
};

} // namespace nextpilot::plotscope::context
// B7 过渡别名：core 消亡前全仓仍以 core::Xxx 取用。簇 8 删 core 时移除。
namespace nextpilot::plotscope::core
{
using context::InteractionRegistry;
} // namespace nextpilot::plotscope::core

