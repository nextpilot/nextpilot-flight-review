#include "CsvFormatDriver.h"
#include "CsvImportProbe.h"

#include "DataRepository.h"
#include "Dataset.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

using namespace nextpilot::plotscope::core;

QString CsvFormatDriver::id() const
{
    return QStringLiteral("csv");
}

QString CsvFormatDriver::displayName() const
{
    return QStringLiteral("CSV 格式驱动");
}

nextpilot::plotscope::core::Domain CsvFormatDriver::domain() const
{
    return nextpilot::plotscope::core::Domain::General;
}

QStringList CsvFormatDriver::suffixes() const
{
    return {QStringLiteral("csv")};
}

bool CsvFormatDriver::acceptsFormat(const QString &format) const
{
    // 兼容默认：空 format 视为 csv（历史调用省略 format 时按 CSV 处理）。
    return format.isEmpty() || format == QStringLiteral("csv") || format == QStringLiteral(".csv");
}

QString CsvFormatDriver::preferredTableName(const Intent &intent, const QString &sourcePath) const
{
    // 表名策略（声明，非执行）：
    //   intent 显式指定 table -> 用它；否则用文件名（去扩展名）。
    // 与旧 driver 路径的行为保持一致。
    const QString explicitName = intent.params.value(QStringLiteral("table")).toString();
    if (!explicitName.isEmpty())
        return explicitName;
    return QFileInfo(sourcePath).completeBaseName();
}

Result<IntentResult> CsvFormatDriver::load(const Intent &intent, DataRepository &repo)
{
    Q_UNUSED(intent);
    Q_UNUSED(repo);
    // CSV 已归并到统一摄取管道（见 parserId()），宿主不会调用本函数。
    // 协议解析在 CsvParser（SPI 实现）里，只有**一份**实现。
    return fail(QStringLiteral("eunsupported"),
                QStringLiteral("CSV 已归并到统一摄取管道，请走 data.load 的管道路径"));
}

// === 导入配置协议：schema + probe + 默认选项 ===
//
// 字段定义与老 CsvDataLoader::schema() 对齐，但这是纯 QJsonObject，
// 不含任何 QWidget / 回调（headless 可用）。GUI 通用对话框按它渲染。

QJsonObject CsvFormatDriver::importSchema() const
{
    QJsonObject s;
    s[QStringLiteral("title")] = QStringLiteral("加载 CSV");

    auto field = [](const QString &key, const QString &type, const QString &label) {
        return QJsonObject{{QStringLiteral("key"), key},
                           {QStringLiteral("type"), type},
                           {QStringLiteral("label"), label}};
    };
    auto addOption = [](QJsonArray &opts, const QString &v, const QString &label) {
        opts.append(QJsonObject{{QStringLiteral("value"), v}, {QStringLiteral("label"), label}});
    };

    QJsonArray fields;

    // 分隔符（探测预填）
    {
        QJsonObject f = field(QStringLiteral("delimiter"), QStringLiteral("enum"), QStringLiteral("分隔符"));
        f[QStringLiteral("default")] = QStringLiteral("auto");
        QJsonArray opts;
        addOption(opts, QStringLiteral("auto"), QStringLiteral("自动探测"));
        addOption(opts, QStringLiteral(","), QStringLiteral("逗号  ,"));
        addOption(opts, QStringLiteral(";"), QStringLiteral("分号  ;"));
        addOption(opts, QStringLiteral("\t"), QStringLiteral("制表符  \\t"));
        addOption(opts, QStringLiteral(" "), QStringLiteral("空格"));
        f[QStringLiteral("options")] = opts;
        f[QStringLiteral("initial_from_probe")] =
            QJsonObject{{QStringLiteral("probe_key"), QStringLiteral("delimiter")},
                        {QStringLiteral("trim"), 1}};
        fields.append(f);
    }
    // 第一行是表头
    {
        QJsonObject f = field(QStringLiteral("has_header"), QStringLiteral("bool"), QStringLiteral("第一行是表头"));
        f[QStringLiteral("default")] = true;
        f[QStringLiteral("initial_from_probe")] =
            QJsonObject{{QStringLiteral("probe_key"), QStringLiteral("has_header")}};
        fields.append(f);
    }
    // 跳过开头行
    {
        QJsonObject f = field(QStringLiteral("skip_rows"), QStringLiteral("int"), QStringLiteral("跳过开头行"));
        f[QStringLiteral("default")] = 0;
        f[QStringLiteral("min")] = 0;
        f[QStringLiteral("max")] = 1000;
        fields.append(f);
    }
    // 时间轴模式
    {
        QJsonObject f = field(QStringLiteral("time.mode"), QStringLiteral("enum"), QStringLiteral("时间轴"));
        f[QStringLiteral("default")] = QStringLiteral("row");
        QJsonArray opts;
        addOption(opts, QStringLiteral("row"), QStringLiteral("行号（不解析时间）"));
        addOption(opts, QStringLiteral("column"), QStringLiteral("指定一列作为时间"));
        addOption(opts, QStringLiteral("combined"), QStringLiteral("日期列 + 时间列合并"));
        f[QStringLiteral("options")] = opts;
        // 探测到明确时间列 → 自动切到 column 模式（老 schema 同款语义）。
        f[QStringLiteral("initial_from_probe")] =
            QJsonObject{{QStringLiteral("probe_key"), QStringLiteral("suggested_time")},
                        {QStringLiteral("set"), QStringLiteral("column")}};
        fields.append(f);
    }
    // 时间列（source = 探测到的列；仅 column 模式显示）
    {
        QJsonObject f = field(QStringLiteral("time.index"), QStringLiteral("enum"), QStringLiteral("时间列"));
        f[QStringLiteral("source")] =
            QJsonObject{{QStringLiteral("probe_key"), QStringLiteral("columns")},
                        {QStringLiteral("label_key"), QStringLiteral("name")},
                        {QStringLiteral("filter_key"), QStringLiteral("kind")}};
        f[QStringLiteral("filter_kind")] = QStringLiteral("all");
        f[QStringLiteral("initial_from_probe")] =
            QJsonObject{{QStringLiteral("probe_key"), QStringLiteral("suggested_time")}};
        f[QStringLiteral("visible_if")] =
            QJsonObject{{QStringLiteral("key"), QStringLiteral("time.mode")},
                        {QStringLiteral("equals"), QStringLiteral("column")}};
        fields.append(f);
    }
    // 日期列 / 时间列（combined 模式显示）
    {
        QJsonObject f = field(QStringLiteral("time.date"), QStringLiteral("enum"), QStringLiteral("日期列"));
        f[QStringLiteral("source")] =
            QJsonObject{{QStringLiteral("probe_key"), QStringLiteral("columns")},
                        {QStringLiteral("label_key"), QStringLiteral("name")},
                        {QStringLiteral("filter_key"), QStringLiteral("kind")}};
        f[QStringLiteral("filter_kind")] = QStringLiteral("date_only");
        f[QStringLiteral("visible_if")] =
            QJsonObject{{QStringLiteral("key"), QStringLiteral("time.mode")},
                        {QStringLiteral("equals"), QStringLiteral("combined")}};
        fields.append(f);
    }
    {
        QJsonObject f = field(QStringLiteral("time.time"), QStringLiteral("enum"), QStringLiteral("时间列（时:分:秒）"));
        f[QStringLiteral("source")] =
            QJsonObject{{QStringLiteral("probe_key"), QStringLiteral("columns")},
                        {QStringLiteral("label_key"), QStringLiteral("name")},
                        {QStringLiteral("filter_key"), QStringLiteral("kind")}};
        f[QStringLiteral("filter_kind")] = QStringLiteral("time_only");
        f[QStringLiteral("visible_if")] =
            QJsonObject{{QStringLiteral("key"), QStringLiteral("time.mode")},
                        {QStringLiteral("equals"), QStringLiteral("combined")}};
        fields.append(f);
    }
    s[QStringLiteral("fields")] = fields;

    // 预览与警告规则
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

QJsonObject CsvFormatDriver::probeFile(const QString &path) const
{
    return nextpilot::plotscope::ext::CsvImportProbe::probe(path);
}

QByteArray CsvFormatDriver::defaultImportOptions(const QString &path) const
{
    const QJsonObject p = nextpilot::plotscope::ext::CsvImportProbe::probe(path);
    if (p.contains(QStringLiteral("error")))
        return QByteArray();
    return nextpilot::plotscope::ext::CsvImportProbe::defaultOptions(p);
}
