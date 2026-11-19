#include "Transform.h"

#include <QHash>
#include <QJsonArray>
#include <QStringList>

namespace nextpilot::plotscope::core
{

namespace
{
QHash<QString, TransformDescriptor> &registry()
{
    static QHash<QString, TransformDescriptor> r;
    return r;
}

// 内置变换定义：一次性注册。全部带能力位与自描述。
void registerBuiltins()
{
    if (!registry().isEmpty())
        return; // 幂等

    auto add = [](const QString &id, const QString &label, TransformKind kind,
                  uint32_t caps, const QString &desc, const QString &example,
                  const QList<TransformParam> &params) {
        TransformDescriptor d;
        d.id = id;
        d.label = label;
        d.kind = kind;
        d.capabilities = caps;
        d.description = desc;
        d.example = example;
        d.params = params;
        d.builtin = true;
        registry().insert(id, d);
    };

    // --- Rust 内置算子（ps-ops/filters 已有）：数据源衍生 + 曲线 X/Y 轴 ---
    add(QStringLiteral("builtin.none"), QStringLiteral("直通（不变换）"),
        TransformKind::Builtin, CapTransform,
        QStringLiteral("原样输出，不做任何处理。用于占位或对比。"),
        QStringLiteral("{}"), {});
    add(QStringLiteral("builtin.absolute"), QStringLiteral("绝对值"),
        TransformKind::Builtin, CapDerived | CapAxisY,
        QStringLiteral("取 |x|。衍生通道与曲线 Y 轴可用；时间轴不适用。"),
        QStringLiteral("{}"), {});
    add(QStringLiteral("builtin.scale"), QStringLiteral("比例 / 偏移"),
        TransformKind::Builtin, CapTransform,
        QStringLiteral("y = value_scale*x + value_offset，时间轴可加 time_offset_sec。"
                       "负号用 value_scale=-1，比例调整改 value_scale。 "
                       "适用：数据源衍生、X/Y 轴、Checksum 前的数值缩放。"),
        QStringLiteral("{\"value_scale\":-1,\"value_offset\":0,\"time_offset_sec\":0}"),
        {
            {QStringLiteral("value_scale"), QStringLiteral("比例"), QStringLiteral("number"),
             QStringLiteral("乘到值上的系数；取 -1 即加负号"), QStringLiteral("1")},
            {QStringLiteral("value_offset"), QStringLiteral("偏移"), QStringLiteral("number"),
             QStringLiteral("加到值上的常量"), QStringLiteral("0")},
            {QStringLiteral("time_offset_sec"), QStringLiteral("时间偏移(秒)"), QStringLiteral("number"),
             QStringLiteral("仅 X 轴/时间轴生效；平移时间基准"), QStringLiteral("0")},
        });
    add(QStringLiteral("builtin.normalize"), QStringLiteral("归一化"),
        TransformKind::Builtin, CapDerived | CapAxisY,
        QStringLiteral("把整段数据线性映射到 [0,1]。需要全段，离线数据适用。"),
        QStringLiteral("{}"), {});
    add(QStringLiteral("builtin.derivative"), QStringLiteral("微分"),
        TransformKind::Builtin, CapDerived | CapAxisY,
        QStringLiteral("dy/dt；可选固定 dt。仅在值序列上有意义。"),
        QStringLiteral("{\"use_custom_dt\":false,\"custom_dt\":1}"),
        {
            {QStringLiteral("use_custom_dt"), QStringLiteral("使用固定 dt"), QStringLiteral("bool"),
             QStringLiteral("勾选后用 custom_dt 代替真实时间差"), QStringLiteral("false")},
            {QStringLiteral("custom_dt"), QStringLiteral("固定 dt"), QStringLiteral("number"),
             QStringLiteral("固定时间步长"), QStringLiteral("1")},
        });
    add(QStringLiteral("builtin.integral"), QStringLiteral("积分"),
        TransformKind::Builtin, CapDerived | CapAxisY,
        QStringLiteral("梯形积分 ∫y dt；可选固定 dt。"),
        QStringLiteral("{\"use_custom_dt\":false,\"custom_dt\":1}"), {});
    add(QStringLiteral("builtin.moving_average"), QStringLiteral("滑动平均"),
        TransformKind::Builtin, CapDerived | CapAxisY,
        QStringLiteral("窗口均值；可补偿时间偏移到窗口中点。"),
        QStringLiteral("{\"window\":10,\"compensate_time_offset\":false}"),
        {
            {QStringLiteral("window"), QStringLiteral("窗口"), QStringLiteral("number"),
             QStringLiteral("滑动窗口点数"), QStringLiteral("10")},
        });
    add(QStringLiteral("builtin.moving_rms"), QStringLiteral("滑动均方根"),
        TransformKind::Builtin, CapDerived | CapAxisY,
        QStringLiteral("窗口 RMS。"),
        QStringLiteral("{\"window\":10}"), {});
    add(QStringLiteral("builtin.moving_variance"), QStringLiteral("滑动方差"),
        TransformKind::Builtin, CapDerived | CapAxisY,
        QStringLiteral("窗口方差，可选输出标准差。"),
        QStringLiteral("{\"window\":10,\"std_dev\":false}"), {});

    // --- 数学表达式：Rust 求值，全场景通用 ---
    add(QStringLiteral("expression"), QStringLiteral("数学表达式"),
        TransformKind::Expression, CapTransform,
        QStringLiteral("对 x 求值：四则/幂/三角/对数等，如 5*x+10/sin(x)。"
                       "变量 x 为当前值；X 轴场景 x 为时间（秒）。"
                       "每次按单点求值，live 与离线都适用。"),
        QStringLiteral("5*x + 10/sin(x)"),
        {
            {QStringLiteral("expr"), QStringLiteral("表达式"), QStringLiteral("string"),
             QStringLiteral("支持 + - * / ^、sin/cos/tan/abs/sqrt/ln/log/exp/min/max/clamp"),
             QStringLiteral("x")},
        });

    // --- Lua：C++ 求值，可在线调试，全场景通用 ---
    add(QStringLiteral("lua.inline"), QStringLiteral("Lua 代码"),
        TransformKind::Lua, CapTransform,
        QStringLiteral("内联 Lua 代码，约定 function transform(x, ctx) return y end。"
                       "支持单点(live)与批量(offline)调试；运算在 C++ 侧，便于在线调参。"),
        QStringLiteral("function transform(x, ctx)\n  return x * 2\nend"),
        {
            {QStringLiteral("code"), QStringLiteral("Lua 代码"), QStringLiteral("string"),
             QStringLiteral("返回数值的函数 transform(x, ctx)"), QString()},
        });
}

} // namespace

QString transformKindName(TransformKind kind)
{
    switch (kind)
    {
    case TransformKind::Builtin:    return QStringLiteral("builtin");
    case TransformKind::Expression: return QStringLiteral("expression");
    case TransformKind::Lua:        return QStringLiteral("lua");
    }
    return QStringLiteral("builtin");
}

TransformKind transformKindFromName(const QString &name)
{
    if (name == QStringLiteral("expression")) return TransformKind::Expression;
    if (name == QStringLiteral("lua"))        return TransformKind::Lua;
    return TransformKind::Builtin;
}

QString capabilityNames(uint32_t caps)
{
    QStringList parts;
    if (caps & CapDerived)  parts << QStringLiteral("衍生");
    if (caps & CapAxisX)    parts << QStringLiteral("轴X");
    if (caps & CapAxisY)    parts << QStringLiteral("轴Y");
    if (caps & CapChecksum) parts << QStringLiteral("Checksum");
    return parts.isEmpty() ? QStringLiteral("无") : parts.join(QStringLiteral("/"));
}

bool capabilitiesContain(uint32_t caps, TransformCapability cap)
{
    return (caps & static_cast<uint32_t>(cap)) != 0;
}

QJsonObject TransformDescriptor::toJson() const
{
    QJsonArray paramsArr;
    for (const TransformParam &p : params)
        paramsArr.append(QJsonObject{
            {QStringLiteral("key"), p.key},
            {QStringLiteral("label"), p.label},
            {QStringLiteral("type"), p.type},
            {QStringLiteral("description"), p.description},
            {QStringLiteral("defaultValue"), p.defaultValue},
        });
    return QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("label"), label},
        {QStringLiteral("kind"), transformKindName(kind)},
        {QStringLiteral("capabilities"), static_cast<int>(capabilities)},
        {QStringLiteral("capabilityNames"), capabilityNames(capabilities)},
        {QStringLiteral("description"), description},
        {QStringLiteral("example"), example},
        {QStringLiteral("params"), paramsArr},
        {QStringLiteral("builtin"), builtin},
    };
}

TransformDescriptor TransformDescriptor::fromJson(const QJsonObject &obj)
{
    TransformDescriptor d;
    d.id = obj.value(QStringLiteral("id")).toString();
    d.label = obj.value(QStringLiteral("label")).toString();
    d.kind = transformKindFromName(obj.value(QStringLiteral("kind")).toString());
    d.capabilities = static_cast<uint32_t>(obj.value(QStringLiteral("capabilities")).toInt());
    d.description = obj.value(QStringLiteral("description")).toString();
    d.example = obj.value(QStringLiteral("example")).toString();
    d.builtin = obj.value(QStringLiteral("builtin")).toBool();
    const QJsonArray paramsArr = obj.value(QStringLiteral("params")).toArray();
    for (const QJsonValue &v : paramsArr)
    {
        const QJsonObject po = v.toObject();
        TransformParam p;
        p.key = po.value(QStringLiteral("key")).toString();
        p.label = po.value(QStringLiteral("label")).toString();
        p.type = po.value(QStringLiteral("type")).toString();
        p.description = po.value(QStringLiteral("description")).toString();
        p.defaultValue = po.value(QStringLiteral("defaultValue")).toString();
        d.params.append(p);
    }
    return d;
}

QJsonObject TransformRequest::toJson() const
{
    return QJsonObject{
        {QStringLiteral("descriptorId"), descriptorId},
        {QStringLiteral("kind"), transformKindName(kind)},
        {QStringLiteral("expression"), expression},
        {QStringLiteral("luaSource"), luaSource},
        {QStringLiteral("scriptId"), scriptId},
        {QStringLiteral("params"), params},
    };
}

TransformRequest TransformRequest::fromJson(const QJsonObject &obj)
{
    TransformRequest r;
    r.descriptorId = obj.value(QStringLiteral("descriptorId")).toString();
    r.kind = transformKindFromName(obj.value(QStringLiteral("kind")).toString());
    r.expression = obj.value(QStringLiteral("expression")).toString();
    r.luaSource = obj.value(QStringLiteral("luaSource")).toString();
    r.scriptId = obj.value(QStringLiteral("scriptId")).toString();
    r.params = obj.value(QStringLiteral("params")).toObject();
    return r;
}

void TransformRegistry::registerDescriptor(const TransformDescriptor &descriptor)
{
    registerBuiltins();
    if (descriptor.id.isEmpty())
        return;
    // 内置定义不允许被覆盖（用户脚本 id 用 lua.<scriptId> 前缀，不会撞）。
    const auto it = registry().constFind(descriptor.id);
    if (it != registry().constEnd() && it.value().builtin)
        return;
    registry().insert(descriptor.id, descriptor);
}

bool TransformRegistry::contains(const QString &id)
{
    registerBuiltins();
    return registry().contains(id);
}

TransformDescriptor TransformRegistry::get(const QString &id)
{
    registerBuiltins();
    return registry().value(id);
}

QList<TransformDescriptor> TransformRegistry::all()
{
    registerBuiltins();
    return registry().values();
}

QList<TransformDescriptor> TransformRegistry::byCapability(TransformCapability cap)
{
    QList<TransformDescriptor> out;
    for (const TransformDescriptor &d : all())
        if (d.supports(cap))
            out.append(d);
    return out;
}

QStringList TransformRegistry::idsByCapability(TransformCapability cap)
{
    QStringList ids;
    for (const TransformDescriptor &d : byCapability(cap))
        ids << d.id;
    return ids;
}

void TransformRegistry::clear()
{
    registry().clear();
}

} // namespace nextpilot::plotscope::core
