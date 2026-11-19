#include "FormSchema.h"
#include "ParamSpec.h"
#include "SchemaEngine.h"
#include "Transform.h"

#include <iostream>

using namespace nextpilot::plotscope::core;
using namespace nextpilot::plotscope::uischema;

namespace
{
int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok)
    {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    }
}
} // namespace

// 验证 TransformParam(魔法字符串) -> ParamSpec(metadata) -> FormSchema 的适配链路。
// 这是 TransformEditorDialog 参数区改用的核心逻辑，headless 可测。
int main()
{
    TransformDescriptor d;
    d.id = QStringLiteral("builtin.scale");
    d.label = QStringLiteral("缩放/偏移");

    TransformParam flag;
    flag.key = QStringLiteral("enabled");
    flag.label = QStringLiteral("启用");
    flag.type = QStringLiteral("bool");
    flag.defaultValue = QStringLiteral("true");
    d.params.append(flag);

    TransformParam factor;
    factor.key = QStringLiteral("factor");
    factor.label = QStringLiteral("缩放系数");
    factor.type = QStringLiteral("number");
    factor.defaultValue = QStringLiteral("2.5");
    d.params.append(factor);

    TransformParam name;
    name.key = QStringLiteral("tag");
    name.label = QStringLiteral("标签");
    name.type = QStringLiteral("string");
    name.defaultValue = QStringLiteral("x");
    d.params.append(name);

    const QVector<ParamSpec> specs = transformParamSpecs(d);
    check(specs.size() == 3, "适配出 3 条规格");
    check(specs.at(0).type == ParamType::Bool, "bool 映射");
    check(specs.at(0).defaultValue.toBool() == true, "bool 默认值转换");
    check(specs.at(1).type == ParamType::Double, "number 映射为 double");
    check(specs.at(1).defaultValue.toDouble() == 2.5, "number 默认值转换");
    check(specs.at(2).type == ParamType::String, "string 映射");

    QHash<QString, QString> labels;
    for (const TransformParam &p : d.params)
        labels.insert(p.key, p.label);
    const FormSchema form = SchemaEngine::compile(d.label, specs, labels);

    check(form.title == QStringLiteral("缩放/偏移"), "descriptor 名作标题");
    check(form.fields.at(0).type == FieldType::Bool, "Bool 字段");
    check(form.fields.at(1).type == FieldType::Number, "Number 字段");
    check(form.fields.at(2).type == FieldType::Text, "Text 字段");
    check(form.fields.at(1).label == QStringLiteral("缩放系数"), "术语表覆盖中文 label");
    check(form.fields.at(0).label == QStringLiteral("启用"), "术语表覆盖 bool label");

    if (failures)
    {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "transform adapter: all checks passed\n";
    return 0;
}
