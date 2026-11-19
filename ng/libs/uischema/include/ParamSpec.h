#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariant>
#include <QVector>

namespace nextpilot::plotscope::uischema
{

// 参数的数据类型。描述值是什么，不描述怎么画。
enum class ParamType
{
    Bool,
    Int,
    Double,
    Enum,
    String,
    File,
};

QString paramTypeName(ParamType type);
ParamType paramTypeFromName(const QString &name);

// 枚举候选：值为回传数据，label 为语义名。
struct EnumOption
{
    QString value;
    QString label;
};

// 参数间的逻辑依赖：本项仅在 key 等于 equals 时有意义。
struct Dependency
{
    QString key;
    QVariant equals;
};

// 一份参数规格。纯 metadata：headless 可构造、序列化、查询。
// 只描述数据（名字/类型/取值/约束/单位/依赖/说明），不含任何控件/布局词汇。
struct ParamSpec
{
    QString key;
    ParamType type = ParamType::String;
    QVariant defaultValue;
    double min = 0;
    double max = 0;
    bool hasRange = false;
    QVector<EnumOption> options;
    QString fileFilter;
    QString unit;
    QVector<Dependency> dependsOn;
    QString description;
};

} // namespace nextpilot::plotscope::uischema
