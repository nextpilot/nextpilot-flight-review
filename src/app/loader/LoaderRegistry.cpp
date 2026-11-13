#include "loader/LoaderRegistry.h"

#include "app/plugin_host/HostApi.h"

#include "ps_plugin_api.h"
#include "app/plugin_host/PluginRuntime.h"
#include "ps_kernel.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>

extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_csv(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_ulog(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_apbin(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_asc(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_blf(void);

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

QString readParams(const PsLoaderVtable *v, const QString &path)
{
    if (!v->params_json)
        return {};
    QByteArray out(1024 * 1024, '\0');
    const int32_t n = v->params_json(path.toUtf8().constData(), out.data(),
                                     static_cast<uint32_t>(out.size()));
    if (n <= 0)
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
    registerDescriptor(ps_plugin_entry_data_loader_ulog());
    registerDescriptor(ps_plugin_entry_data_loader_apbin());
    registerDescriptor(ps_plugin_entry_data_loader_asc());
    registerDescriptor(ps_plugin_entry_data_loader_blf());
    registerDescriptor(ps_plugin_entry_data_loader_csv());
}

bool LoaderRegistry::registerDescriptor(const PsPluginDescriptor *desc, const QString &packageDir)
{
    if (!desc || desc->abi_version != PS_PLUGIN_ABI_VERSION || !desc->loader)
        return false;
    // LoaderRegistry 只收 data_loader；其他 family 由各自的 catalog 管理。
    if (desc->family != PS_FAMILY_DATA_LOADER)
        return false;
    const PsLoaderVtable *v = desc->loader;
    if (v->abi_version != PS_PLUGIN_ABI_VERSION || !v->id || !v->load)
        return false;

    const QString id = QString::fromUtf8(v->id);
    if (findById(id))
        return false;

    LoaderPlugin p;
    p.id = id;
    const QJsonObject manifest = QJsonDocument::fromJson(QByteArray(desc->manifest_json)).object();
    p.version = manifest.value(QStringLiteral("version")).toString(QStringLiteral("0"));
    p.cacheNamespace = id + QLatin1Char('@') + p.version;
    p.family = QStringLiteral("data_loader");
    p.label = QString::fromUtf8(v->label ? v->label : v->id);
    p.filter = QString::fromUtf8(v->filter ? v->filter : "* (*)");
    p.extensions = QString::fromUtf8(v->extensions ? v->extensions : "").split(',', Qt::SkipEmptyParts);
    p.formatTag = QString::fromUtf8(v->format_tag ? v->format_tag : v->label);
    p.tree = v->tree != 0;
    p.hasParams = v->has_params != 0;
    p.cacheable = (desc->capabilities & PS_CAP_CACHE) != 0;
    p.capabilities = desc->capabilities;
    p.schema = readSchema(v);
    p.probe = [v](const QString &path, const QJsonObject &opts) {
        return readProbe(v, path, opts);
    };
    p.defaultOptions = [v](const QString &path) {
        return readDefaultOptions(v, path);
    };
    p.paramsJson = [v](const QString &path) {
        return readParams(v, path);
    };
    if (desc->ui && desc->ui->dialog_ui && !packageDir.isEmpty())
    {
        const PsUiVtable *u = desc->ui;
        p.uiDialog = QDir(packageDir).absoluteFilePath(QString::fromUtf8(u->dialog_ui));
        if (u->get_data)
        {
            p.uiData = [u](const QString &widget) {
                QByteArray out(64 * 1024, '\0');
                const int32_t n = u->get_data(widget.toUtf8().constData(), out.data(),
                                              static_cast<uint32_t>(out.size()));
                if (n <= 0)
                    return QString();
                return QString::fromUtf8(out.constData(), n);
            };
        }
    }
    p.load = [v](const QString &path, const QString &opts, PsDataset **ds,
                 char *err, unsigned int cap) {
        return v->load(path.toUtf8().constData(), opts.toUtf8().constData(), ps_host_api(),
                       reinterpret_cast<void **>(ds), err, cap);
    };
    m_plugins.append(p);
    return true;
}

QStringList LoaderRegistry::scanPlugins(const QStringList &dirs)
{
    PluginRuntime &rt = PluginRuntime::instance();
    QStringList errors;
    for (const QString &dir : dirs)
        rt.scanDirectory(dir.toStdString());

    for (const PsLoadedPlugin &p : rt.plugins())
    {
        if (!p.descriptor || p.descriptor->family != PS_FAMILY_DATA_LOADER)
            continue;
        if (registerDescriptor(p.descriptor, QString::fromStdString(p.packageDir)))
            continue;
        const QString id = p.descriptor->loader && p.descriptor->loader->id
                               ? QString::fromUtf8(p.descriptor->loader->id)
                               : QString();
        if (!findById(id))
            errors << QStringLiteral("%1：注册失败").arg(QString::fromStdString(p.library));
    }
    for (const std::string &e : rt.errors())
        errors << QString::fromStdString(e);
    return errors;
}

const LoaderPlugin *LoaderRegistry::findBySuffix(const QString &suffix) const
{
    const QString dot = suffix.startsWith('.') ? suffix.toLower() : QStringLiteral(".") + suffix.toLower();
    for (const LoaderPlugin &p : m_plugins)
        if (p.extensions.contains(dot, Qt::CaseInsensitive))
            return &p;
    // 未知扩展名兜底给 CSV；解析失败由插件报错。
    for (const LoaderPlugin &p : m_plugins)
        if (p.id == QLatin1String("data_loader_csv"))
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
