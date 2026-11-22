#include "CsvFormatParser.h"
#include "CsvImportProbe.h"
#include "Intent.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;

QString CsvFormatParser::id() const
{
    return QStringLiteral("csv");
}

QString CsvFormatParser::displayName() const
{
    return QStringLiteral("CSV 格式解析器");
}

nextpilot::plotscope::registry::Domain CsvFormatParser::domain() const
{
    return nextpilot::plotscope::registry::Domain::General;
}

QStringList CsvFormatParser::suffixes() const
{
    return {QStringLiteral("csv")};
}

bool CsvFormatParser::acceptsFormat(const QString &format) const
{
    // 兼容默认：空 format 视为 csv（兼容省略 format 的调用）。
    return format.isEmpty() || format == QStringLiteral("csv") || format == QStringLiteral(".csv");
}

QString CsvFormatParser::preferredTableName(const Intent &intent, const QString &sourcePath) const
{
    // 表名策略（声明，非执行）：
    //   intent 显式指定 table -> 用它；否则用文件名（去扩展名）。
    // 与旧 driver 路径的行为保持一致。
    const QString explicitName = intent.params.value(QStringLiteral("table")).toString();
    if (!explicitName.isEmpty())
        return explicitName;
    return QFileInfo(sourcePath).completeBaseName();
}

// === 配置协议：schema + probe + 默认选项 ===
// 纯 QJsonObject，不含 QWidget / 回调（headless 可用）。GUI 通用对话框按它渲染。

QJsonObject CsvFormatParser::configSchema() const
{
    // v2 语法：name/type/label/options/flags/default + 布局容器 + {from} 来源。
    QJsonObject s;
    s[QStringLiteral("title")] = QStringLiteral("加载 CSV");

    // 探测列的公共来源（time.index/date/time 都从 columns 取候选）。
    auto columnSource = [](const QString &kind) {
        return QJsonObject{{QStringLiteral("from"),
                            QJsonObject{{QStringLiteral("probeKey"), QStringLiteral("columns")},
                                        {QStringLiteral("labelKey"), QStringLiteral("name")},
                                        {QStringLiteral("filterKey"), QStringLiteral("kind")},
                                        {QStringLiteral("filterKind"), kind}}}};
    };
    auto dependsOn = [](const QString &equals) {
        return QJsonObject{{QStringLiteral("dependsOn"),
                            QJsonObject{{QStringLiteral("name"), QStringLiteral("time.mode")},
                                        {QStringLiteral("equals"), equals}}}};
    };
    auto option = [](const QString &v, const QString &label) {
        return QJsonObject{{QStringLiteral("value"), v}, {QStringLiteral("label"), label}};
    };

    // 基本段
    QJsonArray basic;
    {
        QJsonObject f{{QStringLiteral("name"), QStringLiteral("delimiter")},
                      {QStringLiteral("type"), QStringLiteral("select")},
                      {QStringLiteral("label"), QStringLiteral("分隔符")},
                      {QStringLiteral("default"), QStringLiteral("auto")}};
        f[QStringLiteral("options")] = QJsonArray{
            option(QStringLiteral("auto"), QStringLiteral("自动探测")),
            option(QStringLiteral(","), QStringLiteral("逗号  ,")),
            option(QStringLiteral(";"), QStringLiteral("分号  ;")),
            option(QStringLiteral("\t"), QStringLiteral("制表符  \\t")),
            option(QStringLiteral(" "), QStringLiteral("空格"))};
        f[QStringLiteral("defaultSource")] =
            QJsonObject{{QStringLiteral("from"),
                         QJsonObject{{QStringLiteral("probeKey"), QStringLiteral("delimiter")},
                                     {QStringLiteral("trim"), 1}}}};
        basic.append(f);
    }
    {
        QJsonObject f{{QStringLiteral("name"), QStringLiteral("has_header")},
                      {QStringLiteral("type"), QStringLiteral("bool")},
                      {QStringLiteral("label"), QStringLiteral("第一行是表头")},
                      {QStringLiteral("default"), true}};
        f[QStringLiteral("defaultSource")] =
            QJsonObject{{QStringLiteral("from"),
                         QJsonObject{{QStringLiteral("probeKey"), QStringLiteral("has_header")}}}};
        basic.append(f);
    }
    {
        QJsonObject f{{QStringLiteral("name"), QStringLiteral("skip_rows")},
                      {QStringLiteral("type"), QStringLiteral("number")},
                      {QStringLiteral("label"), QStringLiteral("跳过开头行")},
                      {QStringLiteral("default"), 0},
                      {QStringLiteral("flags"),
                       QJsonObject{{QStringLiteral("nonNegative"), true},
                                   {QStringLiteral("min"), 0},
                                   {QStringLiteral("max"), 1000}}}};
        basic.append(f);
    }

    // 时间段
    QJsonArray timing;
    {
        QJsonObject f{{QStringLiteral("name"), QStringLiteral("time.mode")},
                      {QStringLiteral("type"), QStringLiteral("select")},
                      {QStringLiteral("label"), QStringLiteral("时间轴")},
                      {QStringLiteral("default"), QStringLiteral("row")}};
        f[QStringLiteral("options")] = QJsonArray{
            option(QStringLiteral("row"), QStringLiteral("行号（不解析时间）")),
            option(QStringLiteral("column"), QStringLiteral("指定一列作为时间")),
            option(QStringLiteral("combined"), QStringLiteral("日期列 + 时间列合并"))};
        // 探测到明确时间列 → 自动切到 column。
        f[QStringLiteral("defaultSource")] =
            QJsonObject{{QStringLiteral("from"),
                         QJsonObject{{QStringLiteral("probeKey"), QStringLiteral("suggested_time")},
                                     {QStringLiteral("when"), QStringLiteral("ge:0")},
                                     {QStringLiteral("set"), QStringLiteral("column")}}}};
        timing.append(f);
    }
    {
        QJsonObject f{{QStringLiteral("name"), QStringLiteral("time.index")},
                      {QStringLiteral("type"), QStringLiteral("select")},
                      {QStringLiteral("label"), QStringLiteral("时间列")},
                      {QStringLiteral("options"), columnSource(QStringLiteral("all"))},
                      {QStringLiteral("flags"), dependsOn(QStringLiteral("column"))}};
        f[QStringLiteral("defaultSource")] =
            QJsonObject{{QStringLiteral("from"),
                         QJsonObject{{QStringLiteral("probeKey"), QStringLiteral("suggested_time")}}}};
        timing.append(f);
    }
    {
        QJsonObject f{{QStringLiteral("name"), QStringLiteral("time.date")},
                      {QStringLiteral("type"), QStringLiteral("select")},
                      {QStringLiteral("label"), QStringLiteral("日期列")},
                      {QStringLiteral("options"), columnSource(QStringLiteral("date_only"))},
                      {QStringLiteral("flags"), dependsOn(QStringLiteral("combined"))}};
        timing.append(f);
    }
    {
        QJsonObject f{{QStringLiteral("name"), QStringLiteral("time.time")},
                      {QStringLiteral("type"), QStringLiteral("select")},
                      {QStringLiteral("label"), QStringLiteral("时间列（时:分:秒）")},
                      {QStringLiteral("options"), columnSource(QStringLiteral("time_only"))},
                      {QStringLiteral("flags"), dependsOn(QStringLiteral("combined"))}};
        timing.append(f);
    }

    // 字段树：基本 + 时间两个 section。
    QJsonArray fields;
    fields.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("basic")},
                              {QStringLiteral("type"), QStringLiteral("section")},
                              {QStringLiteral("label"), QStringLiteral("基本")},
                              {QStringLiteral("fields"), basic}});
    fields.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("timing")},
                              {QStringLiteral("type"), QStringLiteral("section")},
                              {QStringLiteral("label"), QStringLiteral("时间")},
                              {QStringLiteral("fields"), timing}});
    s[QStringLiteral("fields")] = fields;

    // 预览与警告规则（ui 为 schema 顶层扩展，不属字段语法）。
    QJsonObject ui;
    ui[QStringLiteral("preview")] =
        QJsonObject{{QStringLiteral("columns_key"), QStringLiteral("columns")},
                    {QStringLiteral("rows_key"), QStringLiteral("preview")},
                    {QStringLiteral("label_key"), QStringLiteral("name")},
                    {QStringLiteral("summary"), QStringLiteral("探测：{column_count} 列 · {row_count} 行预览")},
                    {QStringLiteral("warnings_key"), QStringLiteral("warnings")}};
    QJsonArray warnRules;
    warnRules.append(QJsonObject{
        {QStringLiteral("probe_key"), QStringLiteral("has_header")},
        {QStringLiteral("probe_equals"), false},
        {QStringLiteral("field_key"), QStringLiteral("has_header")},
        {QStringLiteral("field_equals"), true},
        {QStringLiteral("text"), QStringLiteral("探测认为首行是数据，但勾了“第一行是表头”，请确认。")}});
    ui[QStringLiteral("warnings")] = warnRules;
    s[QStringLiteral("ui")] = ui;
    return s;
}

QJsonObject CsvFormatParser::probeFile(const QString &path) const
{
    return nextpilot::plotscope::ext::CsvImportProbe::probe(path);
}

QByteArray CsvFormatParser::defaultImportOptions(const QString &path) const
{
    const QJsonObject p = nextpilot::plotscope::ext::CsvImportProbe::probe(path);
    if (p.contains(QStringLiteral("error")))
        return QByteArray();
    return nextpilot::plotscope::ext::CsvImportProbe::defaultOptions(p);
}
