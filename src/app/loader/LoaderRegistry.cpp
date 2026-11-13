#include "loader/LoaderRegistry.h"

#include "plugin_sdk/ps_plugin_api.h"
#include "ps_kernel.h"

#include <QJsonDocument>
#include <QJsonObject>

extern "C" const PsPluginDescriptor *ps_plugin_entry_csv(void);

namespace {

QJsonObject readSchema(const PsLoaderVtable *v)
{
    if (!v->schema_json)
        return {};
    QByteArray buf(64 * 1024, '\0');
    const int32_t n = v->schema_json(buf.data(), static_cast<uint32_t>(buf.size()));
    if (n <= 0)
        return {};
    return QJsonDocument::fromJson(QByteArray(buf.constData(), n)).object();
}

QJsonObject readProbe(const PsLoaderVtable *v, const QString &path, const QJsonObject &opts)
{
    if (!v->probe_json)
        return {};
    const QByteArray optsJson = QJsonDocument(opts).toJson(QJsonDocument::Compact);
    QByteArray out(64 * 1024, '\0');
    const int32_t n = v->probe_json(path.toUtf8().constData(), optsJson.constData(),
                                    out.data(), static_cast<uint32_t>(out.size()));
    if (n < 0)
    {
        QJsonObject e;
        e[QStringLiteral("error")] = QStringLiteral("probe 失败");
        return e;
    }
    return QJsonDocument::fromJson(QByteArray(out.constData(), n)).object();
}

QString readDefaultOptions(const PsLoaderVtable *v, const QString &path)
{
    if (!v->default_options)
        return {};
    QByteArray out(16 * 1024, '\0');
    const int32_t n = v->default_options(path.toUtf8().constData(), out.data(),
                                        static_cast<uint32_t>(out.size()));
    if (n < 0)
        return {};
    return QString::fromUtf8(out.constData(), n);
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

    registerDescriptor(ps_plugin_entry_csv());
}

void LoaderRegistry::registerDescriptor(const PsPluginDescriptor *desc)
{
    if (!desc || desc->abi_version != PS_PLUGIN_ABI_VERSION || !desc->loader)
        return;
    const PsLoaderVtable *v = desc->loader;
    if (v->abi_version != PS_PLUGIN_ABI_VERSION || !v->id || !v->load)
        return;

    LoaderPlugin p;
    p.id = QString::fromUtf8(v->id);
    p.label = QString::fromUtf8(v->label ? v->label : v->id);
    p.filter = QString::fromUtf8(v->filter ? v->filter : "* (*)");
    p.formatTag = QString::fromUtf8(v->format_tag ? v->format_tag : v->label);
    p.tree = v->tree != 0;
    p.hasParams = v->has_params != 0;
    p.schema = readSchema(v);
    p.probe = [v](const QString &path, const QJsonObject &opts) {
        return readProbe(v, path, opts);
    };
    p.defaultOptions = [v](const QString &path) {
        return readDefaultOptions(v, path);
    };
    p.load = [v](const QString &path, const QString &opts, PsDataset **ds,
                 char *err, unsigned int cap) {
        return v->load(path.toUtf8().constData(), opts.toUtf8().constData(),
                       reinterpret_cast<void **>(ds), err, cap);
    };
    m_plugins.append(p);
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
