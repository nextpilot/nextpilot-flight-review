#include "FormSchema.h"
#include "ParamSpec.h"
#include "SchemaEngine.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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

QJsonObject option(const QString &value, const QString &label)
{
    return QJsonObject{{QStringLiteral("value"), value}, {QStringLiteral("label"), label}};
}

// 找叶子字段（含容器内），找不到返回 nullptr。
const FieldSpec *find(const QVector<FieldSpec> &fields, const QString &key)
{
    for (const FieldSpec &f : fields)
    {
        if (f.key == key)
            return &f;
        if (const FieldSpec *hit = find(f.fields, key))
            return hit;
    }
    return nullptr;
}
} // namespace

int main()
{
    // === 1. 结构化入口：ParamSpec 树 ===
    QVector<ParamSpec> specs;
    ParamSpec enabled;
    enabled.key = QStringLiteral("auto_detect");
    enabled.type = ParamType::Bool;
    enabled.defaultValue = true;
    specs.append(enabled);

    ParamSpec rate;
    rate.key = QStringLiteral("sample_rate");
    rate.type = ParamType::Number;
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

    ParamSpec mode;
    mode.key = QStringLiteral("mode");
    mode.type = ParamType::Select;
    mode.defaultValue = QStringLiteral("fast");
    mode.options = {{QStringLiteral("fast"), QStringLiteral("快速")},
                    {QStringLiteral("safe"), QStringLiteral("安全")}};
    specs.append(mode);

    FormSchema form = SchemaEngine::compile(QStringLiteral("采集参数"), specs);
    check(form.title == QStringLiteral("采集参数"), "title 传递");
    check(form.fields.size() == 3, "字段数");
    check(form.fields.at(0).type == FieldType::Bool, "Bool -> Bool");
    check(form.fields.at(1).type == FieldType::Number, "Number -> Number");
    check(form.fields.at(2).type == FieldType::Select, "Select -> Select");
    check(form.fields.at(1).label == QStringLiteral("Sample rate"), "key -> label");
    check(form.fields.at(1).unit == QStringLiteral("Hz"), "unit 传递");
    check(form.fields.at(1).defaultValue.toInt() == 100, "int 默认值无损");

    // select 显示 label、回传 value 分离。
    check(form.fields.at(2).options.at(0).value == QStringLiteral("fast"), "select value 保留");
    check(form.fields.at(2).options.at(0).label == QStringLiteral("快速"), "select label 保留");

    // dependsOn：auto_detect=true 时 sample_rate 禁用。
    check(!SchemaEngine::isEnabled(form.fields.at(1), form), "依赖：禁用");
    FormSchema changed = form;
    changed.fields[0].value = false;
    check(SchemaEngine::isEnabled(changed.fields.at(1), changed), "依赖：满足时启用");

    // 按当前值判定（渲染期联动）。
    QHash<QString, QVariant> current{{QStringLiteral("auto_detect"), false}};
    check(SchemaEngine::isEnabled(form.fields.at(1), current), "依赖：当前值判定");

    // JSON 往返。
    const QJsonObject json = form.toJson();
    FormSchema restored = FormSchema::fromJson(json);
    check(QJsonDocument(restored.toJson()) == QJsonDocument(json), "二次序列化一致");

    // 零 UI 词汇。
    const QString dump = QString::fromUtf8(QJsonDocument(json).toJson());
    for (const char *word : {"widget", "control", "visible_if", "placeholder", "QCheckBox"})
        check(!dump.contains(QLatin1String(word)), "无 UI 词汇");

    // === 2. v2 JSON 入口：布局树 + options.from + default.from + flags ===
    QJsonArray basic;
    basic.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("delimiter")},
                             {QStringLiteral("type"), QStringLiteral("SELECT")}, // 大小写不敏感
                             {QStringLiteral("label"), QStringLiteral("分隔符")},
                             {QStringLiteral("default"), QStringLiteral("auto")},
                             {QStringLiteral("options"),
                              QJsonArray{option(QStringLiteral(","), QStringLiteral("逗号")),
                                         option(QStringLiteral(";"), QStringLiteral("分号"))}},
                             {QStringLiteral("defaultFrom"),
                              QJsonObject{{QStringLiteral("from"),
                                           QJsonObject{{QStringLiteral("PROBEKEY"), QStringLiteral("delimiter")},
                                                       {QStringLiteral("TRIM"), 1}}}}}});
    basic.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("skip")},
                             {QStringLiteral("type"), QStringLiteral("number")},
                             {QStringLiteral("flags"),
                              QJsonObject{{QStringLiteral("nonNegative"), true},
                                          {QStringLiteral("min"), 0},
                                          {QStringLiteral("max"), 10}}}});

    QJsonArray timing;
    timing.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("time.index")},
                              {QStringLiteral("type"), QStringLiteral("select")},
                              {QStringLiteral("options"),
                               QJsonObject{{QStringLiteral("from"),
                                            QJsonObject{{QStringLiteral("probeKey"), QStringLiteral("columns")},
                                                        {QStringLiteral("labelKey"), QStringLiteral("name")},
                                                        {QStringLiteral("filterKey"), QStringLiteral("kind")},
                                                        {QStringLiteral("filterKind"), QStringLiteral("all")}}}}},
                              {QStringLiteral("flags"),
                               QJsonObject{{QStringLiteral("dependsOn"),
                                            QJsonObject{{QStringLiteral("name"), QStringLiteral("mode")},
                                                        {QStringLiteral("equals"), QStringLiteral("column")}}}}}});

    QJsonArray fields;
    fields.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("basic")},
                              {QStringLiteral("type"), QStringLiteral("section")},
                              {QStringLiteral("label"), QStringLiteral("基本")},
                              {QStringLiteral("fields"), basic}});
    fields.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("timing")},
                              {QStringLiteral("type"), QStringLiteral("tab")},
                              {QStringLiteral("fields"), timing}});
    QJsonObject schema{{QStringLiteral("title"), QStringLiteral("CSV")},
                       {QStringLiteral("fields"), fields}};

    QJsonArray columns;
    columns.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("时间戳")},
                               {QStringLiteral("kind"), QStringLiteral("time")}});
    columns.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("速度")},
                               {QStringLiteral("kind"), QStringLiteral("value")}});
    QJsonObject probe;
    probe[QStringLiteral("delimiter")] = QStringLiteral(";");
    probe[QStringLiteral("columns")] = columns;

    FormSchema tree = SchemaEngine::compileJson(schema, probe);
    check(tree.fields.size() == 2, "顶层两容器");
    check(tree.fields.at(0).type == FieldType::Section, "section 容器");
    check(tree.fields.at(1).type == FieldType::Tab, "tab 容器");
    check(tree.fields.at(0).fields.size() == 2, "section 内 2 字段");

    const FieldSpec *delim = find(tree.fields, QStringLiteral("delimiter"));
    check(delim && delim->type == FieldType::Select, "SELECT 大小写归一");
    check(delim && delim->options.size() == 2, "静态 options 数");
    check(delim && delim->options.at(0).value == QStringLiteral(","), "静态 options value");
    check(delim && delim->defaultValue.toString() == QStringLiteral(";"), "defaultFrom + trim 覆盖");

    const FieldSpec *skip = find(tree.fields, QStringLiteral("skip"));
    check(skip && skip->nonNegative, "flags 开关型");
    check(skip && skip->hasRange && skip->max == 10, "flags 带参数");

    const FieldSpec *idx = find(tree.fields, QStringLiteral("time.index"));
    check(idx && idx->options.size() == 2, "options.from 展开数");
    check(idx && idx->options.at(0).value == QStringLiteral("0"), "options.from value=下标");
    check(idx && idx->options.at(0).label == QStringLiteral("时间戳"), "options.from label");
    check(idx && !idx->dependsOn.isEmpty(), "dependsOn 解析");

    // 树形 JSON 往返。
    const QJsonObject treeJson = tree.toJson();
    FormSchema treeRestored = FormSchema::fromJson(treeJson);
    check(QJsonDocument(treeRestored.toJson()) == QJsonDocument(treeJson), "树形 JSON 往返一致");

    // specsFromJson：新 schema 也能直接产出 ParamSpec 树（含布局子字段）。
    const QVector<ParamSpec> parsed = SchemaEngine::specsFromJson(schema);
    check(parsed.size() == 2 && parsed.at(0).type == ParamType::Section, "specsFromJson 容器");
    check(parsed.at(0).fields.size() == 2, "specsFromJson 子字段");

    // 类型名大小写不敏感。
    check(paramTypeFromName(QStringLiteral("Select")) == ParamType::Select, "Select 大写归一");
    check(paramTypeFromName(QStringLiteral("NUMBER")) == ParamType::Number, "NUMBER 大写归一");
    check(isLayoutType(ParamType::Horizontal), "布局类型判定");

    if (failures)
    {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "uischema standalone: all checks passed\n";
    return 0;
}
