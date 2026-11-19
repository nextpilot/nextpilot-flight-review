#pragma once

#include "FormSchema.h"
#include "ParamSpec.h"

#include <QHash>
#include <QJsonObject>

namespace nextpilot::plotscope::uischema
{

// 把规格编译成表单模型。headless，无 QWidget。
// 唯一一处 ParamType -> FieldType 约定的持有者，也是 {from} 来源的展开者。
class SchemaEngine
{
public:
    // v2 主入口：直接吃 configSchema() 的 JSON（字段名大小写不敏感），
    // probe 为渲染期来源上下文（含 columns/delimiter/suggested_time 等）。
    static FormSchema compileJson(const QJsonObject &schemaJson, const QJsonObject &probe = {});

    // 结构化入口：从 ParamSpec 树编译。供 Transform/Lua 等直接构造规格的场景。
    // labelOverrides 是术语表：key -> 语义名；字段自带 label 优先。
    static FormSchema compile(const QString &title, const QVector<ParamSpec> &specs,
                              const QHash<QString, QString> &labelOverrides = {},
                              const QJsonObject &probe = {});

    // JSON(v2 字段数组) -> ParamSpec 树。供 IMessageParser::paramSpecs 默认转换用。
    static QVector<ParamSpec> specsFromJson(const QJsonObject &schemaJson);

    // 按依赖判定某字段当前是否应启用（遍历整棵树找被依赖字段的当前值）。
    static bool isEnabled(const FieldSpec &field, const FormSchema &form);

    // 按当前控件值判定（渲染期联动用）：values 是 key -> 当前回传值。
    static bool isEnabled(const FieldSpec &field, const QHash<QString, QVariant> &values);
};

} // namespace nextpilot::plotscope::uischema
