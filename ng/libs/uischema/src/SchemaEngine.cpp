#include "SchemaEngine.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QLatin1Char>

namespace nextpilot::plotscope::uischema
{
namespace
{

// 大小写不敏感取键：先精确命中，再遍历比对 toLower。
QJsonValue ciValue(const QJsonObject &o, const QString &key)
{
    if (o.contains(key))
        return o.value(key);
    const QString lower = key.toLower();
    for (auto it = o.constBegin(); it != o.constEnd(); ++it)
        if (it.key().toLower() == lower)
            return it.value();
    return QJsonValue::Undefined;
}

bool ciHas(const QJsonObject &o, const QString &key)
{
    return !ciValue(o, key).isUndefined();
}

QString ciString(const QJsonObject &o, const QString &key, const QString &fallback = QString())
{
    const QJsonValue v = ciValue(o, key);
    return v.isUndefined() || v.isNull() ? fallback : v.toString(fallback);
}

// key -> 展示名：下划线/点后的段转空格、首字母大写。只处理最后一段。
QString labelFromKey(const QString &key)
{
    QString s = key.section(QLatin1Char('.'), -1);
    s.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (s.isEmpty())
        return s;
    s[0] = s.at(0).toUpper();
    return s;
}

FromSpec fromSpecFromJson(const QJsonValue &v)
{
    FromSpec from;
    if (!v.isObject())
        return from;
    const QJsonObject o = v.toObject();
    from.probeKey = ciString(o, QStringLiteral("probeKey"));
    from.labelKey = ciString(o, QStringLiteral("labelKey"), QStringLiteral("name"));
    from.filterKey = ciString(o, QStringLiteral("filterKey"), QStringLiteral("kind"));
    from.filterKind = ciString(o, QStringLiteral("filterKind"), QStringLiteral("all"));
    from.when = ciString(o, QStringLiteral("when"));
    from.trim = ciValue(o, QStringLiteral("trim")).toInt(0);
    from.set = ciValue(o, QStringLiteral("set")).toVariant();
    from.valid = !from.probeKey.isEmpty();
    return from;
}

// flags 两种写法："readonly|nonNegative" 或 {readonly:true,min:0,...}。
void applyFlags(ParamSpec &spec, const QJsonValue &raw)
{
    QJsonObject flags;
    if (raw.isString())
    {
        const QStringList names = raw.toString().split(QLatin1Char('|'), Qt::SkipEmptyParts);
        for (const QString &n : names)
            flags.insert(n.trimmed().toLower(), true);
    }
    else if (raw.isObject())
    {
        const QJsonObject src = raw.toObject();
        for (auto it = src.constBegin(); it != src.constEnd(); ++it)
            flags.insert(it.key().toLower(), it.value());
    }
    else
        return;

    // flags 内部一律小写键，查询也用小写（大小写不敏感）。
    auto flagOn = [&flags](const char *name) {
        return flags.value(QString::fromLatin1(name).toLower()).toBool();
    };
    spec.required = flagOn("required");
    spec.readonly = flagOn("readonly");
    spec.hidden = flagOn("hidden");
    spec.nonNegative = flagOn("nonnegative");
    if (flags.contains(QStringLiteral("min")) && flags.contains(QStringLiteral("max")))
    {
        spec.min = flags.value(QStringLiteral("min")).toDouble();
        spec.max = flags.value(QStringLiteral("max")).toDouble();
        spec.hasRange = true;
    }
    if (flags.contains(QStringLiteral("precision")))
        spec.precision = flags.value(QStringLiteral("precision")).toInt();
    if (flags.contains(QStringLiteral("columns")))
        spec.columns = flags.value(QStringLiteral("columns")).toInt();
    if (flags.contains(QStringLiteral("filefilter")))
        spec.fileFilter = flags.value(QStringLiteral("filefilter")).toString();

    // dependsOn: {name, equals}
    const QJsonValue dep = flags.value(QStringLiteral("dependson"));
    if (dep.isObject())
    {
        const QJsonObject d = dep.toObject();
        Dependency dependency;
        dependency.key = ciString(d, QStringLiteral("name"));
        dependency.equals = ciValue(d, QStringLiteral("equals")).toVariant();
        if (!dependency.key.isEmpty())
            spec.dependsOn.append(dependency);
    }
}

// 解析 options：静态对象 {value:label} / 数组 [{value,label}] / 动态 {from:{...}}。
void applyOptions(ParamSpec &spec, const QJsonValue &raw)
{
    if (!raw.isObject() && !raw.isArray())
        return;
    if (raw.isObject())
    {
        const QJsonObject o = raw.toObject();
        const QJsonValue from = ciValue(o, QStringLiteral("from"));
        if (!from.isUndefined())
        {
            spec.optionsFrom = fromSpecFromJson(from);
            return;
        }
        // 静态 {value:label}
        for (auto it = o.constBegin(); it != o.constEnd(); ++it)
            spec.options.append({it.key(), it.value().toString(it.key())});
    }
    else
    {
        // 兼容旧 [{value,label}]
        for (const QJsonValue &ov : raw.toArray())
        {
            const QJsonObject o = ov.toObject();
            const QString value = ciString(o, QStringLiteral("value"));
            spec.options.append({value, ciString(o, QStringLiteral("label"), value)});
        }
    }
}

// 解析 default：标量直写 / {from:{...}}。
void applyDefault(ParamSpec &spec, const QJsonValue &raw)
{
    if (raw.isUndefined() || raw.isNull())
        return;
    if (raw.isObject())
    {
        const QJsonObject o = raw.toObject();
        const QJsonValue from = ciValue(o, QStringLiteral("from"));
        if (!from.isUndefined())
        {
            spec.defaultFrom = fromSpecFromJson(from);
            return;
        }
    }
    if (raw.isBool())
        spec.defaultValue = raw.toBool();
    else if (raw.isDouble())
        spec.defaultValue = raw.toDouble();
    else
        spec.defaultValue = raw.toString();
}

// 兼容旧 schema 的 snake_case 键：source/initial_from_probe/visible_if。
void applyLegacyFields(ParamSpec &spec, const QJsonObject &f)
{
    if (spec.optionsFrom.valid || spec.options.isEmpty())
    {
        const QJsonValue src = ciValue(f, QStringLiteral("source"));
        if (!src.isUndefined())
            spec.optionsFrom = fromSpecFromJson(src);
    }
    if (!spec.defaultFrom.valid)
    {
        const QJsonValue init = ciValue(f, QStringLiteral("initial_from_probe"));
        if (!init.isUndefined())
            spec.defaultFrom = fromSpecFromJson(init);
    }
    const QJsonValue vi = ciValue(f, QStringLiteral("visible_if"));
    if (!vi.isUndefined() && vi.isObject())
    {
        const QJsonObject v = vi.toObject();
        Dependency dep;
        dep.key = ciString(v, QStringLiteral("key"));
        dep.equals = ciValue(v, QStringLiteral("equals")).toVariant();
        if (!dep.key.isEmpty())
            spec.dependsOn.append(dep);
    }
    const QJsonValue fk = ciValue(f, QStringLiteral("filter_kind"));
    if (!fk.isUndefined() && spec.optionsFrom.valid)
        spec.optionsFrom.filterKind = fk.toString();
}

ParamSpec specFromJson(const QJsonObject &f)
{
    ParamSpec spec;
    spec.key = ciString(f, QStringLiteral("name"), ciString(f, QStringLiteral("key")));
    spec.type = paramTypeFromName(ciString(f, QStringLiteral("type"),
                                           QStringLiteral("string")));
    spec.label = ciString(f, QStringLiteral("label"));
    spec.unit = ciString(f, QStringLiteral("unit"));
    spec.description = ciString(f, QStringLiteral("description"),
                                ciString(f, QStringLiteral("hint")));
    applyOptions(spec, ciValue(f, QStringLiteral("options")));
    applyDefault(spec, ciValue(f, QStringLiteral("default")));
    // 独立来源键：静态 default 兜底，probe 命中才覆盖（探测预填语义）。
    // 兼容两种写法：defaultFrom:{probeKey,...} 或 defaultFrom:{from:{probeKey,...}}。
    const QJsonValue defFrom = ciValue(f, QStringLiteral("defaultFrom"));
    if (!defFrom.isUndefined())
    {
        QJsonValue inner = defFrom;
        if (defFrom.isObject() && ciHas(defFrom.toObject(), QStringLiteral("from")))
            inner = ciValue(defFrom.toObject(), QStringLiteral("from"));
        spec.defaultFrom = fromSpecFromJson(inner);
    }
    applyFlags(spec, ciValue(f, QStringLiteral("flags")));
    applyLegacyFields(spec, f);

    // min/max 也允许直接写在字段顶层（旧格式）。
    if (!spec.hasRange && ciHas(f, QStringLiteral("min")) && ciHas(f, QStringLiteral("max")))
    {
        spec.min = ciValue(f, QStringLiteral("min")).toDouble();
        spec.max = ciValue(f, QStringLiteral("max")).toDouble();
        spec.hasRange = true;
    }
    if (spec.fileFilter.isEmpty())
        spec.fileFilter = ciString(f, QStringLiteral("filter"));

    // 布局字段：递归解析 fields。
    const QJsonArray children = ciValue(f, QStringLiteral("fields")).toArray();
    for (const QJsonValue &cv : children)
        if (cv.isObject())
            spec.fields.append(specFromJson(cv.toObject()));
    return spec;
}

// 满足 when 条件才应用 set。
bool whenHits(const QString &when, const QJsonValue &probeValue)
{
    if (when.isEmpty())
        return true;
    const QString w = when.trimmed().toLower();
    if (w == QStringLiteral("exists"))
        return !probeValue.isUndefined() && !probeValue.isNull();
    if (w.startsWith(QStringLiteral("ge:")))
        return probeValue.toDouble() >= w.mid(3).toDouble();
    if (w.startsWith(QStringLiteral("gt:")))
        return probeValue.toDouble() > w.mid(3).toDouble();
    return false;
}

// 展开 options 的 from：probe[probeKey] 是对象数组，value=数组下标，label=labelKey 的值。
QVector<EnumOption> expandOptions(const FromSpec &from, const QJsonObject &probe)
{
    QVector<EnumOption> out;
    if (!from.valid)
        return out;
    const QJsonArray items = probe.value(from.probeKey).toArray();
    for (int i = 0; i < items.size(); ++i)
    {
        const QJsonObject item = items.at(i).toObject();
        const QString kind = item.value(from.filterKey).toString();
        if (from.filterKind != QStringLiteral("all") && !from.filterKind.isEmpty()
            && kind != from.filterKind)
            continue;
        out.append({QString::number(i), item.value(from.labelKey).toString()});
    }
    return out;
}

// 展开 default 的 from：按 when/set/trim 处理 probe 值。
QVariant expandDefault(const FromSpec &from, const QJsonObject &probe)
{
    if (!from.valid)
        return QVariant();
    const QJsonValue pv = probe.value(from.probeKey);
    if (pv.isUndefined() || pv.isNull())
        return QVariant();
    if (!whenHits(from.when, pv))
        return QVariant();
    if (!from.set.isNull() && from.set.isValid())
        return from.set;
    QVariant v = pv.toVariant();
    if (from.trim > 0 && v.canConvert<QString>())
        v = v.toString().left(from.trim);
    return v;
}

FieldType fieldTypeFor(ParamType type)
{
    switch (type)
    {
    case ParamType::Bool:       return FieldType::Bool;
    case ParamType::Number:     return FieldType::Number;
    case ParamType::Select:     return FieldType::Select;
    case ParamType::File:       return FieldType::File;
    case ParamType::Text:       return FieldType::MultiLine;
    case ParamType::Tab:        return FieldType::Tab;
    case ParamType::Section:    return FieldType::Section;
    case ParamType::Horizontal: return FieldType::Horizontal;
    case ParamType::Vertical:   return FieldType::Vertical;
    case ParamType::String:     return FieldType::Text;
    }
    return FieldType::Text;
}

FieldSpec fieldFromSpec(const ParamSpec &spec, const QHash<QString, QString> &labels,
                        const QJsonObject &probe)
{
    FieldSpec f;
    f.key = spec.key;
    f.label = !spec.label.isEmpty() ? spec.label
                                    : labels.value(spec.key, labelFromKey(spec.key));
    f.type = fieldTypeFor(spec.type);
    f.value = spec.defaultValue;
    f.defaultValue = spec.defaultValue;
    f.unit = spec.unit;
    f.description = spec.description;
    f.fileFilter = spec.fileFilter;
    f.min = spec.min;
    f.max = spec.max;
    f.hasRange = spec.hasRange;
    f.precision = spec.precision;
    f.dependsOn = spec.dependsOn;
    f.required = spec.required;
    f.hidden = spec.hidden;
    f.nonNegative = spec.nonNegative;
    f.columns = spec.columns;
    f.editable = !spec.readonly;
    f.options = spec.options.isEmpty() ? expandOptions(spec.optionsFrom, probe) : spec.options;
    const QVariant expanded = expandDefault(spec.defaultFrom, probe);
    if (expanded.isValid())
    {
        f.value = expanded;
        f.defaultValue = expanded;
    }
    for (const ParamSpec &child : spec.fields)
        f.fields.append(fieldFromSpec(child, labels, probe));
    return f;
}

const FieldSpec *findField(const QVector<FieldSpec> &fields, const QString &key)
{
    for (const FieldSpec &f : fields)
    {
        if (f.key == key)
            return &f;
        if (const FieldSpec *hit = findField(f.fields, key))
            return hit;
    }
    return nullptr;
}

} // namespace

QString paramTypeName(ParamType type)
{
    switch (type)
    {
    case ParamType::Bool:       return QStringLiteral("bool");
    case ParamType::Number:     return QStringLiteral("number");
    case ParamType::String:     return QStringLiteral("string");
    case ParamType::Text:       return QStringLiteral("text");
    case ParamType::Select:     return QStringLiteral("select");
    case ParamType::File:       return QStringLiteral("file");
    case ParamType::Tab:        return QStringLiteral("tab");
    case ParamType::Section:    return QStringLiteral("section");
    case ParamType::Horizontal: return QStringLiteral("horizontal");
    case ParamType::Vertical:   return QStringLiteral("vertical");
    }
    return QStringLiteral("string");
}

ParamType paramTypeFromName(const QString &name)
{
    const QString n = name.trimmed().toLower();
    if (n == QStringLiteral("bool"))       return ParamType::Bool;
    if (n == QStringLiteral("number")
        || n == QStringLiteral("int")
        || n == QStringLiteral("double"))  return ParamType::Number;
    if (n == QStringLiteral("text"))       return ParamType::Text;
    if (n == QStringLiteral("select")
        || n == QStringLiteral("enum"))    return ParamType::Select;
    if (n == QStringLiteral("file"))       return ParamType::File;
    if (n == QStringLiteral("tab"))        return ParamType::Tab;
    if (n == QStringLiteral("section"))    return ParamType::Section;
    if (n == QStringLiteral("horizontal")) return ParamType::Horizontal;
    if (n == QStringLiteral("vertical"))   return ParamType::Vertical;
    return ParamType::String;
}

bool isLayoutType(ParamType type)
{
    return type == ParamType::Tab || type == ParamType::Section
        || type == ParamType::Horizontal || type == ParamType::Vertical;
}

QString fieldTypeName(FieldType type)
{
    switch (type)
    {
    case FieldType::Text:       return QStringLiteral("text");
    case FieldType::MultiLine:  return QStringLiteral("multiline");
    case FieldType::Number:     return QStringLiteral("number");
    case FieldType::Bool:       return QStringLiteral("bool");
    case FieldType::Select:     return QStringLiteral("select");
    case FieldType::File:       return QStringLiteral("file");
    case FieldType::ReadOnly:   return QStringLiteral("readonly");
    case FieldType::Tab:        return QStringLiteral("tab");
    case FieldType::Section:    return QStringLiteral("section");
    case FieldType::Horizontal: return QStringLiteral("horizontal");
    case FieldType::Vertical:   return QStringLiteral("vertical");
    }
    return QStringLiteral("text");
}

QVector<ParamSpec> SchemaEngine::specsFromJson(const QJsonObject &schemaJson)
{
    QVector<ParamSpec> specs;
    const QJsonArray fields = ciValue(schemaJson, QStringLiteral("fields")).toArray();
    for (const QJsonValue &v : fields)
        if (v.isObject())
            specs.append(specFromJson(v.toObject()));
    return specs;
}

FormSchema SchemaEngine::compileJson(const QJsonObject &schemaJson, const QJsonObject &probe)
{
    FormSchema form;
    form.title = ciString(schemaJson, QStringLiteral("title"));
    form.fields = compile(form.title, specsFromJson(schemaJson), {}, probe).fields;
    return form;
}

FormSchema SchemaEngine::compile(const QString &title, const QVector<ParamSpec> &specs,
                                 const QHash<QString, QString> &labelOverrides,
                                 const QJsonObject &probe)
{
    FormSchema form;
    form.title = title;
    for (const ParamSpec &spec : specs)
        form.fields.append(fieldFromSpec(spec, labelOverrides, probe));
    return form;
}

bool SchemaEngine::isEnabled(const FieldSpec &field, const FormSchema &form)
{
    for (const Dependency &dep : field.dependsOn)
    {
        const FieldSpec *other = findField(form.fields, dep.key);
        if (other && other->value != dep.equals)
            return false;
    }
    return true;
}

bool SchemaEngine::isEnabled(const FieldSpec &field, const QHash<QString, QVariant> &values)
{
    for (const Dependency &dep : field.dependsOn)
    {
        if (values.value(dep.key) != dep.equals)
            return false;
    }
    return true;
}

} // namespace nextpilot::plotscope::uischema
