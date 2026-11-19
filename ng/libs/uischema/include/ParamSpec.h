#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariant>
#include <QVector>

namespace nextpilot::plotscope::uischema
{

// 字段类型。数据字段产生值，布局字段只切分结构。
// Int/Double 是 Number 的同义别名，Enum 是 Select 的同义别名（旧调用方兼容）。
enum class ParamType
{
    Bool,
    String,
    Text,
    Number,
    Int = Number,
    Double = Number,
    Select,
    Enum = Select,
    File,
    Tab,
    Section,
    Horizontal,
    Vertical,
};

QString paramTypeName(ParamType type);
// 大小写不敏感：select/SELECT/Select 等价。
ParamType paramTypeFromName(const QString &name);
bool isLayoutType(ParamType type);

// 下拉候选：value 回传，label 显示。
struct EnumOption
{
    QString value;
    QString label;
};

// 字段依赖：本字段仅在 key 字段回传值等于 equals 时启用。
struct Dependency
{
    QString key;
    QVariant equals;
};

// 动态来源。options 与 default 共用一套 {from} 语法。
// 键名解析大小写不敏感；数据值（probeKey 的内容等）保持原样。
struct FromSpec
{
    QString probeKey;
    QString labelKey;
    QString filterKey;
    QString filterKind;
    QString when; // ge:N / gt:N / exists
    int trim = 0;
    QVariant set;
    bool valid = false;
};

// 一份字段规格。纯 metadata：headless 可构造、序列化、查询。
// 布局字段用 fields 装子字段，可递归；叶子字段产生值。
struct ParamSpec
{
    QString key; // = v2 的 name，支持点路径 time.mode
    ParamType type = ParamType::String;
    QString label; // UI 显示名，缺省由 key 生成
    QVariant defaultValue;
    FromSpec defaultFrom;
    FromSpec optionsFrom;
    double min = 0;
    double max = 0;
    bool hasRange = false;
    int precision = -1; // -1 未指定；>=0 强制小数位，0 视为整数
    QVector<EnumOption> options;
    QString fileFilter;
    QString unit;
    QString description;
    QVector<Dependency> dependsOn;
    bool required = false;
    bool readonly = false;
    bool hidden = false;
    bool nonNegative = false;
    int columns = 0; // horizontal 分栏数，0 按子字段数均分
    QVector<ParamSpec> fields;
};

} // namespace nextpilot::plotscope::uischema
