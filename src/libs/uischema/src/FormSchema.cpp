#include "FormSchema.h"

#include <QJsonArray>

namespace nextpilot::plotscope::uischema
{
namespace
{

QJsonObject fieldToJson(const FieldSpec &f)
{
    QJsonObject o;
    o[QStringLiteral("key")] = f.key;
    o[QStringLiteral("label")] = f.label;
    o[QStringLiteral("type")] = fieldTypeName(f.type);
    o[QStringLiteral("value")] = QJsonValue::fromVariant(f.value);
    o[QStringLiteral("defaultValue")] = QJsonValue::fromVariant(f.defaultValue);
    o[QStringLiteral("editable")] = f.editable;
    if (!f.unit.isEmpty())        o[QStringLiteral("unit")] = f.unit;
    if (!f.description.isEmpty()) o[QStringLiteral("description")] = f.description;
    if (!f.fileFilter.isEmpty())  o[QStringLiteral("fileFilter")] = f.fileFilter;
    if (f.hasRange)
    {
        o[QStringLiteral("min")] = f.min;
        o[QStringLiteral("max")] = f.max;
    }
    if (f.precision >= 0)
        o[QStringLiteral("precision")] = f.precision;
    if (!f.options.isEmpty())
    {
        QJsonArray opts;
        for (const EnumOption &e : f.options)
            opts.append(QJsonObject{{QStringLiteral("value"), e.value},
                                     {QStringLiteral("label"), e.label}});
        o[QStringLiteral("options")] = opts;
    }
    if (!f.dependsOn.isEmpty())
    {
        QJsonArray deps;
        for (const Dependency &d : f.dependsOn)
            deps.append(QJsonObject{{QStringLiteral("name"), d.key},
                                     {QStringLiteral("equals"), QJsonValue::fromVariant(d.equals)}});
        o[QStringLiteral("dependsOn")] = deps;
    }
    if (!f.fields.isEmpty())
    {
        QJsonArray children;
        for (const FieldSpec &c : f.fields)
            children.append(fieldToJson(c));
        o[QStringLiteral("fields")] = children;
    }
    return o;
}

FieldType fieldTypeFromName(const QString &t)
{
    if (t == QStringLiteral("bool"))       return FieldType::Bool;
    if (t == QStringLiteral("number"))     return FieldType::Number;
    if (t == QStringLiteral("multiline"))  return FieldType::MultiLine;
    if (t == QStringLiteral("select"))     return FieldType::Select;
    if (t == QStringLiteral("file"))       return FieldType::File;
    if (t == QStringLiteral("readonly"))   return FieldType::ReadOnly;
    if (t == QStringLiteral("tab"))        return FieldType::Tab;
    if (t == QStringLiteral("section"))    return FieldType::Section;
    if (t == QStringLiteral("horizontal")) return FieldType::Horizontal;
    if (t == QStringLiteral("vertical"))   return FieldType::Vertical;
    return FieldType::Text;
}

FieldSpec fieldFromJson(const QJsonObject &o)
{
    FieldSpec f;
    f.key = o.value(QStringLiteral("key")).toString();
    f.label = o.value(QStringLiteral("label")).toString();
    f.type = fieldTypeFromName(o.value(QStringLiteral("type")).toString());
    f.value = o.value(QStringLiteral("value")).toVariant();
    f.defaultValue = o.value(QStringLiteral("defaultValue")).toVariant();
    f.editable = o.value(QStringLiteral("editable")).toBool(true);
    f.unit = o.value(QStringLiteral("unit")).toString();
    f.description = o.value(QStringLiteral("description")).toString();
    f.fileFilter = o.value(QStringLiteral("fileFilter")).toString();
    f.hasRange = o.contains(QStringLiteral("min")) && o.contains(QStringLiteral("max"));
    f.min = o.value(QStringLiteral("min")).toDouble();
    f.max = o.value(QStringLiteral("max")).toDouble();
    if (o.contains(QStringLiteral("precision")))
        f.precision = o.value(QStringLiteral("precision")).toInt();
    for (const QJsonValue &ov : o.value(QStringLiteral("options")).toArray())
    {
        const QJsonObject e = ov.toObject();
        const QString value = e.value(QStringLiteral("value")).toString();
        f.options.append({value, e.value(QStringLiteral("label")).toString(value)});
    }
    for (const QJsonValue &dv : o.value(QStringLiteral("dependsOn")).toArray())
    {
        const QJsonObject d = dv.toObject();
        f.dependsOn.append({d.value(QStringLiteral("name")).toString(),
                            d.value(QStringLiteral("equals")).toVariant()});
    }
    for (const QJsonValue &cv : o.value(QStringLiteral("fields")).toArray())
        if (cv.isObject())
            f.fields.append(fieldFromJson(cv.toObject()));
    return f;
}

} // namespace

QJsonObject FormSchema::toJson() const
{
    QJsonArray arr;
    for (const FieldSpec &f : fields)
        arr.append(fieldToJson(f));
    QJsonObject root;
    root[QStringLiteral("title")] = title;
    root[QStringLiteral("fields")] = arr;
    return root;
}

FormSchema FormSchema::fromJson(const QJsonObject &obj)
{
    FormSchema form;
    form.title = obj.value(QStringLiteral("title")).toString();
    for (const QJsonValue &v : obj.value(QStringLiteral("fields")).toArray())
        if (v.isObject())
            form.fields.append(fieldFromJson(v.toObject()));
    return form;
}

} // namespace nextpilot::plotscope::uischema
