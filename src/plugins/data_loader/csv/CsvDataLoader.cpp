#include "ps_plugin_api.h"

#include "ps_kernel.h"

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
        fields.append(f);
    }
    {
        QJsonObject f = field(QStringLiteral("has_header"), QStringLiteral("bool"),
                              QStringLiteral("第一行是表头"));
        f[QStringLiteral("default")] = true;
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
        fields.append(f);
    }
    {
        QJsonObject f = field(QStringLiteral("time.index"), QStringLiteral("enum"),
                              QStringLiteral("时间列"));
        f[QStringLiteral("source")] = QStringLiteral("columns");
        f[QStringLiteral("column_filter")] = QStringLiteral("all");
        QJsonObject vi;
        vi[QStringLiteral("key")] = QStringLiteral("time.mode");
        vi[QStringLiteral("equals")] = QStringLiteral("column");
        f[QStringLiteral("visible_if")] = vi;
        fields.append(f);
    }
    {
        QJsonObject f = field(QStringLiteral("time.date"), QStringLiteral("enum"),
                              QStringLiteral("日期列"));
        f[QStringLiteral("source")] = QStringLiteral("columns");
        f[QStringLiteral("column_filter")] = QStringLiteral("date_only");
        QJsonObject vi;
        vi[QStringLiteral("key")] = QStringLiteral("time.mode");
        vi[QStringLiteral("equals")] = QStringLiteral("combined");
        f[QStringLiteral("visible_if")] = vi;
        fields.append(f);
    }
    {
        QJsonObject f = field(QStringLiteral("time.time"), QStringLiteral("enum"),
                              QStringLiteral("时间列（时:分:秒）"));
        f[QStringLiteral("source")] = QStringLiteral("columns");
        f[QStringLiteral("column_filter")] = QStringLiteral("time_only");
        QJsonObject vi;
        vi[QStringLiteral("key")] = QStringLiteral("time.mode");
        vi[QStringLiteral("equals")] = QStringLiteral("combined");
        f[QStringLiteral("visible_if")] = vi;
        fields.append(f);
    }

    s[QStringLiteral("fields")] = fields;
    return s;
}

QJsonObject probe(const QString &path, const QJsonObject &opts)
{
    char err[256] = {0};
    QByteArray buf(1 << 16, '\0');
    const QString delim = opts.value(QStringLiteral("delimiter")).toString();
    char force = 0;
    if (delim != QLatin1String("auto") && !delim.isEmpty())
        force = delim.at(0).toLatin1();
    const int32_t n = ps_csv_probe(path.toUtf8().constData(), force, buf.data(),
                                   static_cast<uint32_t>(buf.size()), err, sizeof(err));
    if (n < 0)
    {
        QJsonObject e;
        e[QStringLiteral("error")] = QString::fromUtf8(err);
        return e;
    }
    QJsonObject out = QJsonDocument::fromJson(QByteArray(buf.constData(), n)).object();
    out[QStringLiteral("_opts")] = opts;
    return out;
}

QByteArray defaultOptions(const QString &path)
{
    QJsonObject o;
    QJsonObject opts;
    opts[QStringLiteral("delimiter")] = QStringLiteral("auto");
    const QJsonObject p = probe(path, opts);
    if (p.contains(QStringLiteral("error")))
        return {};

    const QString delim = p.value(QStringLiteral("delimiter")).toString();
    o[QStringLiteral("delimiter")] = delim.isEmpty() ? QStringLiteral(",") : delim.left(1);
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
    if (!out || json.isEmpty() || static_cast<uint32_t>(json.size()) > cap)
        return -1;
    memcpy(out, json.constData(), static_cast<size_t>(json.size()));
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

int32_t csvLoad(const char *path, const char *optsJson, void **dataset,
                char *err, uint32_t errCap)
{
    return ps_csv_load_opts(path, optsJson ? optsJson : "", reinterpret_cast<PsDataset **>(dataset),
                            err, errCap);
}

const PsLoaderVtable kCsvLoader = {
    PS_PLUGIN_ABI_VERSION,
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

const PsPluginDescriptor kCsvPlugin = {
    PS_PLUGIN_ABI_VERSION,
    R"({"id":"data_loader_csv","name":"CSV","version":"1.0.0","sdk":"1.0","family":"data_loader","kind":"file","entry":"ps_plugin_entry_data_loader_csv"})",
    PS_FAMILY_DATA_LOADER,
    PS_CAP_FILE | PS_CAP_PROBE | PS_CAP_SCHEMA | PS_CAP_CACHE,
    &kCsvLoader,
    nullptr,
};

} // namespace

extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_csv(void)
{
    return &kCsvPlugin;
}
