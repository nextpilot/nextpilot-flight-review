#include "FormSchema.h"

#include <QJsonArray>

namespace nextpilot::plotscope::uischema
{

QJsonObject FormSchema::toJson() const
{
    QJsonArray arr;
    for (const FieldSpec &f : fields)
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
        if (!f.options.isEmpty())
            o[QStringLiteral("options")] = QJsonArray::fromStringList(f.options);
        arr.append(o);
    }
    QJsonObject root;
    root[QStringLiteral("title")] = title;
    root[QStringLiteral("fields")] = arr;
    return root;
}

FormSchema FormSchema::fromJson(const QJsonObject &obj)
{
    FormSchema form;
    form.title = obj.value(QStringLiteral("title")).toString();
    const QJsonArray arr = obj.value(QStringLiteral("fields")).toArray();
    for (const QJsonValue &v : arr)
    {
        const QJsonObject o = v.toObject();
        FieldSpec f;
        f.key = o.value(QStringLiteral("key")).toString();
        f.label = o.value(QStringLiteral("label")).toString();
        const QString t = o.value(QStringLiteral("type")).toString();
        if (t == QStringLiteral("bool"))     f.type = FieldType::Bool;
        else if (t == QStringLiteral("number"))   f.type = FieldType::Number;
        else if (t == QStringLiteral("enum"))     f.type = FieldType::Enum;
        else if (t == QStringLiteral("file"))     f.type = FieldType::File;
        else if (t == QStringLiteral("readonly")) f.type = FieldType::ReadOnly;
        else                                      f.type = FieldType::Text;
        f.value = o.value(QStringLiteral("value")).toVariant();
        f.defaultValue = o.value(QStringLiteral("defaultValue")).toVariant();
        f.editable = o.value(QStringLiteral("editable")).toBool(true);
        f.unit = o.value(QStringLiteral("unit")).toString();
        f.description = o.value(QStringLiteral("description")).toString();
        f.fileFilter = o.value(QStringLiteral("fileFilter")).toString();
        f.hasRange = o.contains(QStringLiteral("min")) && o.contains(QStringLiteral("max"));
        f.min = o.value(QStringLiteral("min")).toDouble();
        f.max = o.value(QStringLiteral("max")).toDouble();
        const QJsonArray opts = o.value(QStringLiteral("options")).toArray();
        for (const QJsonValue &ov : opts)
            f.options.append(ov.toString());
        form.fields.append(f);
    }
    return form;
}

} // namespace nextpilot::plotscope::uischema
