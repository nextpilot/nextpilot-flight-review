#include "FormSchema.h"
#include "ParamSpec.h"
#include "SchemaEngine.h"

#include <QJsonDocument>

#include <iostream>

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

int main()
{
    // 一份纯 metadata 规格：覆盖各类型 + 值域 + 依赖 + 单位 + 文件过滤。
    QVector<ParamSpec> specs;

    ParamSpec enabled;
    enabled.key = QStringLiteral("auto_detect");
    enabled.type = ParamType::Bool;
    enabled.defaultValue = true;
    enabled.description = QStringLiteral("自动探测协议");
    specs.append(enabled);

    ParamSpec rate;
    rate.key = QStringLiteral("sample_rate");
    rate.type = ParamType::Int;
    rate.defaultValue = 100;
    rate.min = 1;
    rate.max = 1000;
    rate.hasRange = true;
    rate.unit = QStringLiteral("Hz");
    Dependency dep;
    dep.key = QStringLiteral("auto_detect");
    dep.equals = false;
    rate.dependsOn.append(dep);
    specs.append(rate);

    ParamSpec gain;
    gain.key = QStringLiteral("gain");
    gain.type = ParamType::Double;
    gain.defaultValue = 2.5;
    specs.append(gain);

    ParamSpec mode;
    mode.key = QStringLiteral("mode");
    mode.type = ParamType::Enum;
    mode.defaultValue = QStringLiteral("fast");
    mode.options = {{QStringLiteral("fast"), QStringLiteral("快速")},
                    {QStringLiteral("safe"), QStringLiteral("安全")}};
    specs.append(mode);

    ParamSpec db;
    db.key = QStringLiteral("database_path");
    db.type = ParamType::File;
    db.fileFilter = QStringLiteral("*.db");
    specs.append(db);

    // compile：metadata -> FormSchema。
    FormSchema form = SchemaEngine::compile(QStringLiteral("采集参数"), specs);
    check(form.title == QStringLiteral("采集参数"), "title 传递");
    check(form.fields.size() == 5, "字段数");
    check(form.fields.at(0).type == FieldType::Bool, "Bool -> Bool");
    check(form.fields.at(1).type == FieldType::Number, "Int -> Number");
    check(form.fields.at(2).type == FieldType::Number, "Double -> Number");
    check(form.fields.at(3).type == FieldType::Enum, "Enum -> Enum");
    check(form.fields.at(4).type == FieldType::File, "File -> File");

    // label 由 key 生成，spec 不写 label。
    check(form.fields.at(1).label == QStringLiteral("Sample rate"), "key -> label");
    check(form.fields.at(1).unit == QStringLiteral("Hz"), "unit 传递");

    // 类型化值保留。
    check(form.fields.at(1).defaultValue.toInt() == 100, "int 默认值无损");
    check(form.fields.at(2).defaultValue.toDouble() == 2.5, "double 默认值无损");

    // Enum 取语义 label。
    check(form.fields.at(3).options.contains(QStringLiteral("快速")), "Enum 语义名");

    // 依赖判定：auto_detect=true 时 sample_rate 禁用。
    check(!SchemaEngine::isEnabled(form.fields.at(1), form), "依赖：禁用");
    FormSchema changed = form;
    changed.fields[0].value = false;
    check(SchemaEngine::isEnabled(changed.fields.at(1), changed), "依赖：满足时启用");

    // JSON 往返。
    const QJsonObject json = form.toJson();
    FormSchema restored = FormSchema::fromJson(json);
    check(restored.fields.size() == form.fields.size(), "往返字段数");
    check(QJsonDocument(restored.toJson()) == QJsonDocument(json), "二次序列化一致");

    // 零 UI 词汇：序列化结果不含任何控件/布局字段名。
    const QString dump = QString::fromUtf8(QJsonDocument(json).toJson());
    for (const char *word : {"widget", "control", "layout", "visible_if", "placeholder", "QCheckBox", "row", "column"})
        check(!dump.contains(QLatin1String(word)), "无 UI 词汇");

    if (failures)
    {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "uischema standalone: all checks passed\n";
    return 0;
}
