#include "loader/LoaderRegistry.h"

#include "ps_kernel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

QJsonObject csvSchema()
{
    QJsonObject s;
    s[QStringLiteral("id")] = QStringLiteral("csv");
    s[QStringLiteral("title")] = QStringLiteral("加载 CSV");
    QJsonArray fields;

    auto field = [](const QString &key, const QString &type, const QString &label) {
        QJsonObject f;
        f[QStringLiteral("key")] = key;
        f[QStringLiteral("type")] = type;
        f[QStringLiteral("label")] = label;
        return f;
    };
    {
        QJsonObject f = field(QStringLiteral("delimiter"), QStringLiteral("enum"), QStringLiteral("分隔符"));
        f[QStringLiteral("default")] = QStringLiteral("auto");
        QJsonArray opts;
        auto add = [&opts](const QString &v, const QString &t) {
            QJsonObject o;
            o[QStringLiteral("value")] = v;
            o[QStringLiteral("label")] = t;
            opts.append(o);
        };
        add(QStringLiteral("auto"), QStringLiteral("自动探测"));
        add(QStringLiteral(","), QStringLiteral("逗号  ,"));
        add(QStringLiteral(";"), QStringLiteral("分号  ;"));
        add(QStringLiteral("\t"), QStringLiteral("制表符  \\t"));
        add(QStringLiteral(" "), QStringLiteral("空格"));
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
        auto add = [&opts](const QString &v, const QString &t) {
            QJsonObject o;
            o[QStringLiteral("value")] = v;
            o[QStringLiteral("label")] = t;
            opts.append(o);
        };
        add(QStringLiteral("row"), QStringLiteral("行号（不解析时间）"));
        add(QStringLiteral("column"), QStringLiteral("指定一列作为时间"));
        add(QStringLiteral("combined"), QStringLiteral("日期列 + 时间列合并"));
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

QJsonObject probeCsv(const QString &path, const QJsonObject &opts)
{
    char err[256] = {0};
    QByteArray buf(1 << 16, '\0');
    QString delim = opts.value(QStringLiteral("delimiter")).toString();
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
    QJsonObject probe = QJsonDocument::fromJson(QByteArray(buf.constData(), n)).object();
    probe[QStringLiteral("_opts")] = opts;
    return probe;
}

QString defaultCsvOptions(const QString &path)
{
    QJsonObject o;
    QJsonObject opts;
    opts[QStringLiteral("delimiter")] = QStringLiteral("auto");
    QJsonObject probe = probeCsv(path, opts);
    if (probe.contains(QStringLiteral("error")))
        return QString();

    QString delim = probe.value(QStringLiteral("delimiter")).toString();
    o[QStringLiteral("delimiter")] = delim.isEmpty() ? QStringLiteral(",") : delim.left(1);
    o[QStringLiteral("has_header")] = probe.value(QStringLiteral("has_header")).toBool(true);
    o[QStringLiteral("skip_rows")] = 0;
    QJsonObject t;
    if (probe.contains(QStringLiteral("suggested_time")))
    {
        t[QStringLiteral("mode")] = QStringLiteral("column");
        t[QStringLiteral("index")] = probe.value(QStringLiteral("suggested_time")).toInt();
    }
    else
        t[QStringLiteral("mode")] = QStringLiteral("row");
    o[QStringLiteral("time")] = t;
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

} // namespace

LoaderRegistry &LoaderRegistry::instance()
{
    static LoaderRegistry r;
    return r;
}

LoaderRegistry::LoaderRegistry()
{
    auto addSimple = [this](const QString &id, const QString &label, const QString &filter,
                            const QString &tag, bool tree, bool params,
                            int (*fn)(const char *, PsDataset **, char *, uint32_t)) {
        LoaderPlugin p;
        p.id = id;
        p.label = label;
        p.filter = filter;
        p.formatTag = tag;
        p.tree = tree;
        p.hasParams = params;
        p.load = [fn](const QString &path, const QString &, PsDataset **ds, char *err, unsigned int cap) {
            return fn(path.toUtf8().constData(), ds, err, cap);
        };
        m_plugins.append(p);
    };

    addSimple(QStringLiteral("ulog"), QStringLiteral("ULog"),
              QStringLiteral("ULog (*.ulg);;全部 (*)"), QStringLiteral("ULog"), true, true,
              ps_ulog_load);
    addSimple(QStringLiteral("apbin"), QStringLiteral("DataFlash"),
              QStringLiteral("ArduPilot BIN (*.bin);;全部 (*)"),
              QStringLiteral("ArduPilot · BIN"), true, true, ps_apbin_load);
    addSimple(QStringLiteral("asc"), QStringLiteral("ASC · CAN"),
              QStringLiteral("Vector ASC (*.asc);;全部 (*)"),
              QStringLiteral("ASC · CAN"), true, false, ps_asc_load);
    addSimple(QStringLiteral("blf"), QStringLiteral("BLF · CAN"),
              QStringLiteral("Vector BLF (*.blf);;全部 (*)"),
              QStringLiteral("BLF · CAN"), true, false, ps_blf_load);

    LoaderPlugin csv;
    csv.id = QStringLiteral("csv");
    csv.label = QStringLiteral("CSV");
    csv.filter = QStringLiteral("分隔文本 (*.csv *.tsv *.txt);;全部 (*)");
    csv.formatTag = QStringLiteral("CSV");
    csv.tree = false;
    csv.hasParams = false;
    csv.schema = csvSchema();
    csv.probe = probeCsv;
    csv.defaultOptions = defaultCsvOptions;
    csv.load = [](const QString &path, const QString &opts, PsDataset **ds, char *err, unsigned int cap) {
        return ps_csv_load_opts(path.toUtf8().constData(), opts.toUtf8().constData(), ds, err, cap);
    };
    m_plugins.append(csv);
}

const LoaderPlugin *LoaderRegistry::findBySuffix(const QString &suffix) const
{
    const QString s = suffix.toLower();
    for (const LoaderPlugin &p : m_plugins)
    {
        if (p.id == s)
            return &p;
        if (p.id == QLatin1String("apbin") && s == QLatin1String("bin"))
            return &p;
        if (p.id == QLatin1String("csv") && (s == QLatin1String("tsv") || s == QLatin1String("txt")))
            return &p;
    }
    // 未知扩展名沿用旧的兜底：按 CSV 尝试，失败由解析器报错
    for (const LoaderPlugin &p : m_plugins)
        if (p.id == QLatin1String("csv"))
            return &p;
    return nullptr;
}

const LoaderPlugin *LoaderRegistry::findById(const QString &id) const
{
    for (const LoaderPlugin &p : m_plugins)
        if (p.id == id)
            return &p;
    return nullptr;
}
