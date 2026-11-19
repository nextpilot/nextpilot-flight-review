#pragma once

#include "ParamSpec.h"

#include <QJsonObject>
#include <QString>
#include <QVariant>
#include <QVector>

namespace nextpilot::plotscope::uischema
{

// 字段的呈现类型。由 SchemaEngine 依据 ParamType 决定，不是手写指定。
enum class FieldType
{
    Text,
    Number,
    Bool,
    Enum,
    File,
    ReadOnly,
};

QString fieldTypeName(FieldType type);

// 编译后的一个字段。纯数据，不含 QWidget。
struct FieldSpec
{
    QString key;
    QString label;
    FieldType type = FieldType::Text;
    QVariant value;
    QVariant defaultValue;
    bool editable = true;
    QString unit;
    QString description;
    QStringList options;
    QString fileFilter;
    double min = 0;
    double max = 0;
    bool hasRange = false;
    QVector<Dependency> dependsOn;
};

// 一份表单文档。纯数据，可 JSON 往返，headless 可见。
struct FormSchema
{
    QString title;
    QVector<FieldSpec> fields;

    bool empty() const { return fields.isEmpty(); }
    QJsonObject toJson() const;
    static FormSchema fromJson(const QJsonObject &obj);
};

} // namespace nextpilot::plotscope::uischema
