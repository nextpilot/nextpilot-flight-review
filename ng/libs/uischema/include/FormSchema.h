#pragma once

#include "ParamSpec.h"

#include <QJsonObject>
#include <QString>
#include <QVariant>
#include <QVector>

namespace nextpilot::plotscope::uischema
{

// 编译后的字段呈现类型。由 SchemaEngine 依据 ParamType 决定。
// 数据字段与布局字段都用它，布局字段的 type 直接来自 ParamType 的布局值。
enum class FieldType
{
    Text,       // string -> QLineEdit
    MultiLine,  // text -> QPlainTextEdit
    Number,     // number -> QSpinBox/QDoubleSpinBox
    Bool,       // bool -> QCheckBox
    Select,     // select -> QComboBox（显示 label，回传 value）
    File,       // file -> 文本框 + 浏览按钮
    ReadOnly,
    Tab,        // QTabWidget
    Section,    // QGroupBox
    Horizontal, // QHBoxLayout
    Vertical,   // QVBoxLayout
};

QString fieldTypeName(FieldType type);

// 编译后的一个字段。纯数据，不含 QWidget。布局字段带子 fields。
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
    QVector<EnumOption> options; // value -> label 分离
    QString fileFilter;
    double min = 0;
    double max = 0;
    bool hasRange = false;
    int precision = -1;
    QVector<Dependency> dependsOn;
    bool required = false;
    bool hidden = false;
    bool nonNegative = false;
    int columns = 0;
    QVector<FieldSpec> fields; // 布局容器的子字段
};

// 一份表单文档。纯数据，可 JSON 往返，headless 可见。fields 为树。
struct FormSchema
{
    QString title;
    QVector<FieldSpec> fields;

    bool empty() const { return fields.isEmpty(); }
    QJsonObject toJson() const;
    static FormSchema fromJson(const QJsonObject &obj);
};

} // namespace nextpilot::plotscope::uischema
