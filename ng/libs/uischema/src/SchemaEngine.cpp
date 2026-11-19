#include "SchemaEngine.h"

#include <QHash>

namespace nextpilot::plotscope::uischema
{

QString paramTypeName(ParamType type)
{
    switch (type)
    {
    case ParamType::Bool:   return QStringLiteral("bool");
    case ParamType::Int:    return QStringLiteral("int");
    case ParamType::Double: return QStringLiteral("double");
    case ParamType::Enum:   return QStringLiteral("enum");
    case ParamType::String: return QStringLiteral("string");
    case ParamType::File:   return QStringLiteral("file");
    }
    return QStringLiteral("string");
}

ParamType paramTypeFromName(const QString &name)
{
    if (name == QStringLiteral("bool"))   return ParamType::Bool;
    if (name == QStringLiteral("int"))    return ParamType::Int;
    if (name == QStringLiteral("double")) return ParamType::Double;
    if (name == QStringLiteral("enum"))   return ParamType::Enum;
    if (name == QStringLiteral("file"))   return ParamType::File;
    return ParamType::String;
}

QString fieldTypeName(FieldType type)
{
    switch (type)
    {
    case FieldType::Text:     return QStringLiteral("text");
    case FieldType::Number:   return QStringLiteral("number");
    case FieldType::Bool:     return QStringLiteral("bool");
    case FieldType::Enum:     return QStringLiteral("enum");
    case FieldType::File:     return QStringLiteral("file");
    case FieldType::ReadOnly: return QStringLiteral("readonly");
    }
    return QStringLiteral("text");
}

// 唯一一处 ParamType → FieldType 映射。
static FieldType fieldTypeFor(ParamType type)
{
    switch (type)
    {
    case ParamType::Bool:   return FieldType::Bool;
    case ParamType::Int:
    case ParamType::Double: return FieldType::Number;
    case ParamType::Enum:   return FieldType::Enum;
    case ParamType::File:   return FieldType::File;
    case ParamType::String: return FieldType::Text;
    }
    return FieldType::Text;
}

// key → 展示名：下划线转空格、首字母大写。引擎负责，spec 不写 label。
static QString labelFromKey(const QString &key)
{
    QString s = key;
    s.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (s.isEmpty())
        return s;
    s[0] = s.at(0).toUpper();
    return s;
}

FormSchema SchemaEngine::compile(const QString &title, const QVector<ParamSpec> &specs,
                                 const QHash<QString, QString> &labelOverrides)
{
    FormSchema form;
    form.title = title;
    for (const ParamSpec &spec : specs)
    {
        FieldSpec f;
        f.key = spec.key;
        f.label = labelOverrides.value(spec.key, labelFromKey(spec.key));
        f.type = fieldTypeFor(spec.type);
        f.value = spec.defaultValue;
        f.defaultValue = spec.defaultValue;
        f.unit = spec.unit;
        f.description = spec.description;
        f.fileFilter = spec.fileFilter;
        f.min = spec.min;
        f.max = spec.max;
        f.hasRange = spec.hasRange;
        f.dependsOn = spec.dependsOn;
        for (const EnumOption &opt : spec.options)
            f.options.append(opt.label.isEmpty() ? opt.value : opt.label);
        form.fields.append(f);
    }
    return form;
}

bool SchemaEngine::isEnabled(const FieldSpec &field, const FormSchema &form)
{
    for (const Dependency &dep : field.dependsOn)
    {
        for (const FieldSpec &other : form.fields)
        {
            if (other.key == dep.key && other.value != dep.equals)
                return false;
        }
    }
    return true;
}

} // namespace nextpilot::plotscope::uischema
