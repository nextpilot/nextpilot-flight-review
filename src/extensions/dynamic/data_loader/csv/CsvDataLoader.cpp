#include "ps_extension_api.h"
#include "LegacyFileInput.hpp"

#include "ps_kernel.h"
#include "CsvParser.h"
#include "PsLoaderCommon.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <cstring>

namespace {

QJsonObject schema()
{
    QJsonObject s;
    s[QStringLiteral("id")] = QStringLiteral("data_loader_csv");
    s[QStringLiteral("title")] = QStringLiteral("加载 CSV");
    QJsonArray fields;

    auto field = [](const QString &key, const QString &type, const QString &label) {
        QJsonObject f;
        f[QStringLiteral("key")] = key;
        f[QStringLiteral("type")] = type;
        f[QStringLiteral("label")] = label;
        return f;
    };
    auto addOption = [](QJsonArray &opts, const QString &v, const QString &t) {
        QJsonObject o;
        o[QStringLiteral("value")] = v;
        o[QStringLiteral("label")] = t;
        opts.append(o);
    };
    auto columnsSource = [] {
        QJsonObject o;
        o[QStringLiteral("probe_key")] = QStringLiteral("columns");
        o[QStringLiteral("label_key")] = QStringLiteral("name");
        o[QStringLiteral("filter_key")] = QStringLiteral("kind");
        return o;
    };

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
        QJsonObject init;
        init[QStringLiteral("probe_key")] = QStringLiteral("delimiter");
        init[QStringLiteral("trim")] = 1;
        f[QStringLiteral("initial_from_probe")] = init;
        fields.append(f);
    }
    {
        QJsonObject f = field(QStringLiteral("has_header"), QStringLiteral("bool"),
                              QStringLiteral("第一行是表头"));
        f[QStringLiteral("default")] = true;
        QJsonObject init;
        init[QStringLiteral("probe_key")] = QStringLiteral("has_header");
        f[QStringLiteral("initial_from_probe")] = init;
        fields.append(f);
    }
    {
        QJsonObject f = field(QStringLiteral("skip_rows"), QStringLiteral("int"),
                              QStringLiteral("跳过开头行"));
        f[QStringLiteral("default")] = 0;
        f[QStringLiteral("min")] = 0;
        f[QStringLiteral("max")] = 1000;
        fields.append(f);
    }
    {
        QJsonObject f = field(QStringLiteral("time.mode"), QStringLiteral("enum"),
                              QStringLiteral("时间轴"));
        f[QStringLiteral("default")] = QStringLiteral("row");
        QJsonArray opts;
        addOption(opts, QStringLiteral("row"), QStringLiteral("行号（不解析时间）"));
        addOption(opts, QStringLiteral("column"), QStringLiteral("指定一列作为时间"));
        addOption(opts, QStringLiteral("combined"), QStringLiteral("日期列 + 时间列合并"));
        f[QStringLiteral("options")] = opts;
        QJsonObject init;
        init[QStringLiteral("probe_key")] = QStringLiteral("suggested_time");
        init[QStringLiteral("set")] = QStringLiteral("column");
        f[QStringLiteral("initial_from_probe")] = init;
        fields.append(f);
    }
    {
        QJsonObject f = field(QStringLiteral("time.index"), QStringLiteral("enum"),
                              QStringLiteral("时间列"));
        f[QStringLiteral("source")] = columnsSource();
        f[QStringLiteral("filter_kind")] = QStringLiteral("all");
        QJsonObject init;
        init[QStringLiteral("probe_key")] = QStringLiteral("suggested_time");
        f[QStringLiteral("initial_from_probe")] = init;
        QJsonObject vi;
        vi[QStringLiteral("key")] = QStringLiteral("time.mode");
        vi[QStringLiteral("equals")] = QStringLiteral("column");
        f[QStringLiteral("visible_if")] = vi;
        fields.append(f);
    }
    {
        QJsonObject f = field(QStringLiteral("time.date"), QStringLiteral("enum"),
                              QStringLiteral("日期列"));
        f[QStringLiteral("source")] = columnsSource();
        f[QStringLiteral("filter_kind")] = QStringLiteral("date_only");
        QJsonObject vi;
        vi[QStringLiteral("key")] = QStringLiteral("time.mode");
        vi[QStringLiteral("equals")] = QStringLiteral("combined");
        f[QStringLiteral("visible_if")] = vi;
        fields.append(f);
    }
    {
        QJsonObject f = field(QStringLiteral("time.time"), QStringLiteral("enum"),
                              QStringLiteral("时间列（时:分:秒）"));
        f[QStringLiteral("source")] = columnsSource();
        f[QStringLiteral("filter_kind")] = QStringLiteral("time_only");
        QJsonObject vi;
        vi[QStringLiteral("key")] = QStringLiteral("time.mode");
        vi[QStringLiteral("equals")] = QStringLiteral("combined");
        f[QStringLiteral("visible_if")] = vi;
        fields.append(f);
    }

    s[QStringLiteral("fields")] = fields;

    QJsonObject preview;
    preview[QStringLiteral("columns_key")] = QStringLiteral("columns");
    preview[QStringLiteral("rows_key")] = QStringLiteral("preview");
    preview[QStringLiteral("label_key")] = QStringLiteral("name");
    preview[QStringLiteral("summary")] = QStringLiteral("探测：{column_count} 列 · {row_count} 行预览");
    preview[QStringLiteral("warnings_key")] = QStringLiteral("warnings");

    QJsonArray warnRules;
    QJsonObject warn;
    warn[QStringLiteral("probe_key")] = QStringLiteral("has_header");
    warn[QStringLiteral("probe_equals")] = false;
    warn[QStringLiteral("field_key")] = QStringLiteral("has_header");
    warn[QStringLiteral("field_equals")] = true;
    warn[QStringLiteral("text")] = QStringLiteral("探测认为首行是数据，但勾了“第一行是表头”，请确认。");
    warnRules.append(warn);

    QJsonObject ui;
    ui[QStringLiteral("preview")] = preview;
    ui[QStringLiteral("warnings")] = warnRules;
    s[QStringLiteral("ui")] = ui;
    return s;
}

QJsonObject probe(const QString &path, const QJsonObject &opts)
{
    CsvProbe p;
    std::string err;
    const QString delim = opts.value(QStringLiteral("delimiter")).toString();
    char force = 0;
    if (delim != QLatin1String("auto") && !delim.isEmpty())
        force = delim.at(0).toLatin1();
    if (!psCsvProbeFile(path.toUtf8().constData(), force, p, err))
    {
        QJsonObject e;
        e[QStringLiteral("error")] = QString::fromStdString(err);
        return e;
    }
    QJsonObject out;
    out[QStringLiteral("delimiter")] = QString(QChar(p.delimiter));
    out[QStringLiteral("delimiter_ambiguous")] = p.ambiguous;
    out[QStringLiteral("has_header")] = p.hasHeader;
    QJsonArray cols;
    for (const CsvColumn &c : p.columns)
    {
        QJsonObject o;
        o[QStringLiteral("name")] = QString::fromStdString(c.name);
        o[QStringLiteral("kind")] = QString::fromStdString(c.kind);
        cols.append(o);
    }
    out[QStringLiteral("columns")] = cols;
    QJsonArray rows;
    for (const auto &row : p.preview)
    {
        QJsonArray r;
        for (const auto &cell : row)
            r.append(QString::fromStdString(cell));
        rows.append(r);
    }
    out[QStringLiteral("preview")] = rows;
    if (p.suggestedTime >= 0)
        out[QStringLiteral("suggested_time")] = p.suggestedTime;
    QJsonArray warnings;
    for (const auto &w : p.warnings)
        warnings.append(QString::fromStdString(w));
    out[QStringLiteral("warnings")] = warnings;
    out[QStringLiteral("_opts")] = opts;
    return out;
}

QByteArray defaultOptions(const QString &path)
{
    QJsonObject opts;
    opts[QStringLiteral("delimiter")] = QStringLiteral("auto");
    const QJsonObject p = probe(path, opts);
    if (p.contains(QStringLiteral("error")))
        return {};
    QJsonObject o;
    o[QStringLiteral("delimiter")] = p.value(QStringLiteral("delimiter")).toString();
    o[QStringLiteral("has_header")] = p.value(QStringLiteral("has_header")).toBool(true);
    o[QStringLiteral("skip_rows")] = 0;
    QJsonObject t;
    if (p.contains(QStringLiteral("suggested_time")))
    {
        t[QStringLiteral("mode")] = QStringLiteral("column");
        t[QStringLiteral("index")] = p.value(QStringLiteral("suggested_time")).toInt();
    }
    else
    {
        t[QStringLiteral("mode")] = QStringLiteral("row");
    }
    o[QStringLiteral("time")] = t;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

int32_t writeJson(const QByteArray &json, char *out, uint32_t cap)
{
    if (!out || json.isEmpty() || static_cast<uint32_t>(json.size()) >= cap)
        return -1;
    memcpy(out, json.constData(), static_cast<size_t>(json.size()));
    out[json.size()] = '\0';
    return static_cast<int32_t>(json.size());
}

int32_t csvSchemaJson(char *out, uint32_t cap)
{
    return writeJson(QJsonDocument(schema()).toJson(QJsonDocument::Compact), out, cap);
}

int32_t csvProbeJson(const char *path, const char *optsJson, char *out, uint32_t cap)
{
    const QJsonObject opts = QJsonDocument::fromJson(QByteArray(optsJson ? optsJson : "")).object();
    return writeJson(QJsonDocument(probe(QString::fromUtf8(path), opts)).toJson(QJsonDocument::Compact),
                     out, cap);
}

int32_t csvDefaultOptions(const char *path, char *out, uint32_t cap)
{
    return writeJson(defaultOptions(QString::fromUtf8(path)), out, cap);
}

int32_t csvLoad(const char *path, const char *optsJson, const PsHostApi *host, void **dataset,
                char *err, uint32_t errCap)
{
    const QJsonObject opts = QJsonDocument::fromJson(QByteArray(optsJson ? optsJson : "")).object();
    CsvOptions o;
    const QString delim = opts.value(QStringLiteral("delimiter")).toString();
    if (!delim.isEmpty())
        o.delimiter = delim.at(0).toLatin1();
    o.hasHeader = opts.value(QStringLiteral("has_header")).toBool(true);
    o.skipRows = opts.value(QStringLiteral("skip_rows")).toInt(0);
    const QJsonObject time = opts.value(QStringLiteral("time")).toObject();
    const QString mode = time.value(QStringLiteral("mode")).toString(QStringLiteral("row"));
    if (mode == QLatin1String("column"))
    {
        o.timeMode = 1;
        o.timeIndex = time.value(QStringLiteral("index")).toInt(0);
    }
    else if (mode == QLatin1String("combined"))
    {
        o.timeMode = 2;
        o.dateIndex = time.value(QStringLiteral("date")).toInt(0);
        o.clockIndex = time.value(QStringLiteral("time")).toInt(1);
    }

    CsvTable table;
    std::string perr;
    if (!psCsvParseFile(path, o, table, perr))
    {
        psWriteErr(err, errCap, perr);
        return 1;
    }
    if (table.columns.size() < 2)
    {
        psWriteErr(err, errCap, "没有可画的数值列");
        return 2;
    }
    std::vector<PsChannelData> channels;
    const auto &timeCol = table.columns[0];
    for (size_t i = 1; i < table.columns.size(); ++i)
    {
        PsChannelData c;
        c.name = table.headers[i];
        c.unit = psUnitFromHeader(c.name);
        c.t = timeCol;
        c.v = table.columns[i];
        channels.push_back(std::move(c));
    }
    PsDataset *ds = psBuildDataset(channels, perr, host);
    if (!ds)
    {
        psWriteErr(err, errCap, perr);
        return 3;
    }
    *dataset = ds;
    return 0;
}

const PsLoaderVtable kCsvLoader = {
    PS_EXTENSION_ABI_VERSION,
    "data_loader_csv",
    "CSV",
    "分隔文本 (*.csv *.tsv *.txt);;全部 (*)",
    ".csv,.tsv,.txt",
    "CSV",
    0,
    0,
    {0, 0, 0, 0, 0, 0},
    csvSchemaJson,
    csvProbeJson,
    csvDefaultOptions,
    nullptr,
    csvLoad,
};

const PsDataSourceVtable *csvInputVtable()
{
    static const PsDataSourceVtable vt = ps::VtableBuilder::make<LegacyFileInput<&kCsvLoader>>(R"({"id":"data_loader_csv","name":"CSV","version":"1.0.0","sdk":"1.0","kind":"file"})");
    return &vt;
}

const PsExtensionDescriptor kCsvExtension = {
    PS_EXTENSION_ABI_VERSION,
    R"({"id":"data_loader_csv","name":"CSV","version":"1.0.0","sdk":"1.0","family":"data_source","kind":"file","entry":"ps_extension_entry_data_loader_csv"})",
    PS_FAMILY_DATA_SOURCE,
    PS_CAP_FILE | PS_CAP_PROBE | PS_CAP_SCHEMA | PS_CAP_CACHE,
    &kCsvLoader,
    nullptr,
    nullptr,
    csvInputVtable(),
};

} // namespace

extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_csv(void)
{
    return &kCsvExtension;
}
