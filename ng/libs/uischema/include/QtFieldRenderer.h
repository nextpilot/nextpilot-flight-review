#pragma once

#include "FormSchema.h"

#include <QWidget>

class QFormLayout;

namespace nextpilot::plotscope::uischema
{

// 按 FormSchema 生成编辑控件。全仓唯一的 FieldType → QWidget 映射。
// 控件用 dynamicProperty "fieldKey" 回写 key，读取值统一走 valueOf/setValue。
class QtFieldRenderer
{
public:
    // 把 form 的字段渲染进 layout，返回字段容器 widget 供读取。
    static QWidget *render(const FormSchema &form, QFormLayout *layout, QWidget *parent);

    static QVariant valueOf(QWidget *fieldWidget);
    static void setValue(QWidget *fieldWidget, const QVariant &value);
    static QString keyOf(QWidget *fieldWidget);
    static void applyEnabled(const FormSchema &form, QWidget *host);
};

} // namespace nextpilot::plotscope::uischema
