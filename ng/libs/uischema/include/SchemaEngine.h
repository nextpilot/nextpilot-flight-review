#pragma once

#include "FormSchema.h"
#include "ParamSpec.h"

#include <QHash>

namespace nextpilot::plotscope::uischema
{

// 把纯 metadata 规格编译成表单模型。headless，无 QWidget。
// 唯一一处 ParamType → FieldType 约定的持有者。
class SchemaEngine
{
public:
    // labelOverrides 是术语表：key -> 语义名，未提供时由 key 生成。
    static FormSchema compile(const QString &title, const QVector<ParamSpec> &specs,
                              const QHash<QString, QString> &labelOverrides = {});

    // 供 render 层用：按依赖判定某字段当前是否应启用。
    static bool isEnabled(const FieldSpec &field, const FormSchema &form);
};

} // namespace nextpilot::plotscope::uischema
