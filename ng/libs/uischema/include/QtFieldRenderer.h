#pragma once

#include "FormSchema.h"

#include <QWidget>

class QFormLayout;

namespace nextpilot::plotscope::uischema
{

// 按 FormSchema 生成编辑控件。全仓唯一的 FieldType -> QWidget 映射。
// 递归渲染字段树：布局字段建容器，叶子字段建控件并带 dynamicProperty "fieldKey"。
// 读取值统一走 valueOf/setValue，收集方按 keyOf 过滤。
class QtFieldRenderer
{
public:
    // 把 form 的字段树渲染进 layout。
    static void render(const FormSchema &form, QFormLayout *layout, QWidget *parent);

    static QVariant valueOf(QWidget *fieldWidget);
    static void setValue(QWidget *fieldWidget, const QVariant &value);
    static QString keyOf(QWidget *fieldWidget);

    // 按 dependsOn 刷新整棵树的启用态。
    static void applyEnabled(const FormSchema &form, QWidget *host);
};

} // namespace nextpilot::plotscope::uischema
