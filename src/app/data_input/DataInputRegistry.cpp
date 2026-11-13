#include "data_input/DataInputRegistry.h"

#include "app/plugin_host/HostApi.h"
#include "app/plugin_host/PluginRuntime.h"
#include "ps_kernel.h"
#include "ps_plugin_api.h"

#include <QByteArray>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>
#include <cstring>

extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_csv(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_ulog(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_apbin(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_asc(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_blf(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_load_mcap(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_source_sim(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_source_mavlink(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_source_oem7(void);

namespace {

constexpr int kSchemaCap = 64 * 1024;
constexpr int kParamCap = 1024 * 1024;

QJsonObject readSchema(const PsLoaderVtable *v)
{
    if (!v->schema_json)
        return {};
    QByteArray buf(kSchemaCap, '\0');
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
    QByteArray out(kSchemaCap, '\0');
    const int32_t n = v->probe_json(path.toUtf8().constData(), optsJson.constData(),
                                    out.data(), static_cast<uint32_t>(out.size()));
    if (n < 0)
        return QJsonObject{{QStringLiteral("error"), QStringLiteral("probe 失败")}};
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
    QByteArray out(kParamCap, '\0');
    const int32_t n = v->params_json(path.toUtf8().constData(), out.data(),
                                     static_cast<uint32_t>(out.size()));
    if (n <= 0)
        return {};
    return QString::fromUtf8(out.constData(), n);
}

QString readJson(const std::function<int32_t(char *, uint32_t)> &fn)
{
    if (!fn)
        return {};
    QByteArray buf(kSchemaCap, '\0');
    const int32_t n = fn(buf.data(), static_cast<uint32_t>(buf.size()));
    if (n <= 0)
        return {};
    return QString::fromUtf8(buf.constData(), static_cast<int>(n));
}

QString manifestVersion(const PsPluginDescriptor *desc)
{
    const QJsonObject manifest = QJsonDocument::fromJson(QByteArray(desc->manifest_json)).object();
    return manifest.value(QStringLiteral("version")).toString(QStringLiteral("0"));
}

} // namespace

DataInputRegistry &DataInputRegistry::instance()
{
    static DataInputRegistry r;
    return r;
}

DataInputRegistry::DataInputRegistry()
{
    registerDescriptor(ps_plugin_entry_data_loader_ulog());
    registerDescriptor(ps_plugin_entry_data_loader_apbin());
    registerDescriptor(ps_plugin_entry_data_loader_asc());
    registerDescriptor(ps_plugin_entry_data_loader_blf());
    registerDescriptor(ps_plugin_entry_data_loader_csv());
    registerDescriptor(ps_plugin_entry_data_load_mcap());
    registerDescriptor(ps_plugin_entry_data_source_sim());
    registerDescriptor(ps_plugin_entry_data_source_mavlink());
    registerDescriptor(ps_plugin_entry_data_source_oem7());
}

bool DataInputRegistry::registerDescriptor(const PsPluginDescriptor *desc, const QString &packageDir)
{
    if (!desc || desc->abi_version != PS_PLUGIN_ABI_VERSION)
        return false;

    if (desc->family == PS_FAMILY_DATA_LOADER)
    {
        const PsLoaderVtable *v = desc->loader;
        if (!v || v->abi_version != PS_PLUGIN_ABI_VERSION || !v->id || !v->load)
            return false;
        const QString id = QString::fromUtf8(v->id);
        if (findFileById(id))
            return false;

        DataInputPlugin p;
        p.id = id;
        p.version = manifestVersion(desc);
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
        p.probe = [v](const QString &path, const QJsonObject &opts) { return readProbe(v, path, opts); };
        p.defaultOptions = [v](const QString &path) { return readDefaultOptions(v, path); };
        p.paramsJson = [v](const QString &path) { return readParams(v, path); };
        if (desc->ui && desc->ui->dialog_ui && !packageDir.isEmpty())
        {
            const PsUiVtable *u = desc->ui;
            p.uiDialog = QDir(packageDir).absoluteFilePath(QString::fromUtf8(u->dialog_ui));
            if (u->get_data)
            {
                p.uiData = [u](const QString &widget) {
                    QByteArray out(kSchemaCap, '\0');
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
        m_filePlugins.append(p);
        return true;
    }

    if (desc->family == PS_FAMILY_DATA_INPUT)
    {
        const PsDataInputVtable *v = desc->data_input;
        if (!v || v->protocol_version != PS_DATA_INPUT_PROTOCOL_VERSION ||
            v->struct_size < sizeof(PsDataInputVtable) || !v->create || !v->destroy ||
            !v->capabilities || !v->start || !v->stop || !v->poll || !v->current_state)
            return false;

        void *probe = v->create();
        if (!probe)
            return false;
        const QJsonObject manifest = QJsonDocument::fromJson(QByteArray(desc->manifest_json)).object();
        const uint64_t caps = v->capabilities(probe);
        PsInputDialog dlg = v->get_dialog ? v->get_dialog(probe) : PsInputDialog{nullptr, nullptr, nullptr};
        v->destroy(probe);

        if (caps & PS_INPUT_FILE && !(caps & (PS_INPUT_STREAM | PS_INPUT_REPLAY)))
        {
            const PsLoaderVtable *legacy = desc->loader;
            if (!v->import_file)
                return false;
            const QString manifestId = manifest.value(QStringLiteral("id")).toString();
            if (legacy ? !legacy->load || findFileById(QString::fromUtf8(legacy->id))
                       : manifestId.isEmpty() || findFileById(manifestId))
                return false;
            DataInputPlugin p;
            if (legacy)
            {
                p.id = QString::fromUtf8(legacy->id);
                p.version = manifest.value(QStringLiteral("version")).toString(QStringLiteral("0"));
                p.cacheNamespace = p.id + QLatin1Char('@') + p.version;
                p.family = QStringLiteral("data_input");
                p.label = QString::fromUtf8(legacy->label);
                p.filter = QString::fromUtf8(legacy->filter);
                for (const QString &e : QString::fromUtf8(legacy->extensions).split(QLatin1Char(','), Qt::SkipEmptyParts))
                    p.extensions << (e.startsWith(QLatin1Char('.')) ? e.toLower() : QString(QLatin1Char('.')) + e.toLower());
                p.formatTag = QString::fromUtf8(legacy->format_tag);
                p.tree = legacy->tree != 0;
                p.hasParams = legacy->has_params != 0;
            }
            else
            {
                p.id = manifestId;
                p.version = manifest.value(QStringLiteral("version")).toString(QStringLiteral("0"));
                p.cacheNamespace = p.id + QLatin1Char('@') + p.version;
                p.family = QStringLiteral("data_input");
                p.label = manifest.value(QStringLiteral("name")).toString(manifestId);
                QStringList exts;
                for (const QJsonValue &e : manifest.value(QStringLiteral("file_extensions")).toArray())
                {
                    QString ext = e.toString().toLower();
                    if (!ext.startsWith(QLatin1Char('.')))
                        ext.prepend(QLatin1Char('.'));
                    if (!ext.isEmpty())
                        exts << ext;
                }
                p.extensions = exts;
                p.filter = p.label + QStringLiteral(" (*") + exts.join(QStringLiteral(" *")) + QLatin1Char(')');
                p.formatTag = p.label;
                p.tree = false;
                p.hasParams = false;
            }
            p.cacheable = (desc->capabilities & PS_CAP_CACHE) != 0;
            p.capabilities = desc->capabilities;
            p.uiDialog = dlg.ui_path ? QString::fromUtf8(dlg.ui_path) : QString();
            if (dlg.schema_json)
                p.schema = QJsonDocument::fromJson(QByteArray(dlg.schema_json)).object();
            else if (legacy && legacy->schema_json)
            {
                QByteArray buf(64 * 1024, '\0');
                const int32_t n = legacy->schema_json(buf.data(), static_cast<uint32_t>(buf.size()));
                if (n > 0)
                    p.schema = QJsonDocument::fromJson(QByteArray(buf.constData(), n)).object();
            }
            if (legacy)
            {
                p.probe = [legacy](const QString &path, const QJsonObject &opts) {
                    if (!legacy->probe_json)
                        return QJsonObject{};
                    const QByteArray o = QJsonDocument(opts).toJson(QJsonDocument::Compact);
                    QByteArray buf(64 * 1024, '\0');
                    const int32_t n = legacy->probe_json(path.toUtf8().constData(), o.constData(),
                                                          buf.data(), static_cast<uint32_t>(buf.size()));
                    return n > 0 ? QJsonDocument::fromJson(QByteArray(buf.constData(), n)).object() : QJsonObject{};
                };
                p.defaultOptions = [legacy](const QString &path) {
                    if (!legacy->default_options)
                        return QStringLiteral("{}");
                    QByteArray buf(16 * 1024, '\0');
                    const int32_t n = legacy->default_options(path.toUtf8().constData(), buf.data(),
                                                               static_cast<uint32_t>(buf.size()));
                    return n > 0 ? QString::fromUtf8(buf.constData(), n) : QStringLiteral("{}");
                };
                p.paramsJson = [legacy](const QString &path) {
                    if (!legacy->params_json)
                        return QString();
                    QByteArray buf(1024 * 1024, '\0');
                    const int32_t n = legacy->params_json(path.toUtf8().constData(), buf.data(),
                                                           static_cast<uint32_t>(buf.size()));
                    return n > 0 ? QString::fromUtf8(buf.constData(), n) : QString();
                };
            }
            else
            {
                const QString defaults = QString::fromUtf8(dlg.default_config_json ? dlg.default_config_json : "{}");
                p.probe = [](const QString &, const QJsonObject &) { return QJsonObject{}; };
                p.defaultOptions = [defaults](const QString &) { return defaults; };
                p.paramsJson = [](const QString &) { return QString(); };
            }
            p.load = [v](const QString &path, const QString &opts, PsDataset **ds,
                         char *err, unsigned int errCap) {
                PsError pe{};
                PsDataInputServices services{};
                services.abi_version = PS_HOST_API_VERSION;
                services.host = ps_host_api();
                void *ctx = v->create();
                if (!ctx)
                    return 1;
                if (!v->bind(ctx, services, &pe))
                {
                    if (err && errCap) std::snprintf(err, errCap, "%s", pe.message);
                    v->destroy(ctx);
                    return 1;
                }
                const QByteArray o = opts.isEmpty() ? QByteArrayLiteral("{}") : opts.toUtf8();
                const int32_t rc = v->import_file(ctx, path.toUtf8().constData(), o.constData(),
                                                   reinterpret_cast<void **>(ds), err, errCap);
                v->destroy(ctx);
                return rc;
            };
            m_filePlugins.append(p);
            return true;
        }

        // 流 / 回放插件进数据源列表。
        if (!(caps & (PS_INPUT_STREAM | PS_INPUT_REPLAY)))
            return false;
        const QString id = manifest.value(QStringLiteral("id")).toString();
        if (id.isEmpty() || findSourceById(id))
            return false;

        DataInputPlugin p;
        p.id = id;
        p.version = manifest.value(QStringLiteral("version")).toString(QStringLiteral("0"));
        p.label = manifest.value(QStringLiteral("name")).toString(id);
        p.kind = manifest.value(QStringLiteral("kind")).toString(QStringLiteral("source"));
        p.transport = manifest.value(QStringLiteral("transport")).toString(QStringLiteral("internal"));
        p.recordable = (caps & PS_INPUT_RECORD) != 0;
        p.replayable = (caps & PS_INPUT_REPLAY) != 0;
        p.capabilities = static_cast<uint32_t>(caps);
        const QJsonArray exts = manifest.value(QStringLiteral("file_extensions")).toArray();
        QStringList extList;
        for (const QJsonValue &e : exts)
            extList << e.toString();
        p.replayExtensions = extList.join(QStringLiteral(", "));
        p.schema = QJsonDocument::fromJson(QByteArray(dlg.schema_json ? dlg.schema_json : "")).object();
        const QString defaults = QString::fromUtf8(dlg.default_config_json ? dlg.default_config_json : "{}");
        p.defaultConfig = [defaults] { return defaults; };
        p.open = [v](const QString &config, const PsSourceSink *sink, void **handle, QString *err) {
            char e[256] = {0};
            void *ctx = v->create();
            if (!ctx) {
                if (err) *err = QStringLiteral("create 失败");
                return 1;
            }
            PsDataInputServices services{};
            services.abi_version = PS_HOST_API_VERSION;
            services.host = ps_host_api();
            services.sink = sink;
            PsError pe{};
            if (!v->bind(ctx, services, &pe)) {
                if (err) *err = QString::fromUtf8(pe.message[0] ? pe.message : "bind failed");
                v->destroy(ctx);
                return 1;
            }
            const QByteArray cfgBytes = config.isEmpty() ? QByteArrayLiteral("{}") : config.toUtf8();
            PsStringView sv{cfgBytes.constData(), static_cast<uint32_t>(cfgBytes.size())};
            if (!v->load_config(ctx, sv, &pe)) {
                if (err) *err = QString::fromUtf8(pe.message[0] ? pe.message : "load_config failed");
                v->destroy(ctx);
                return 1;
            }
            if (!v->start(ctx, &pe)) {
                if (err) *err = QString::fromUtf8(pe.message[0] ? pe.message : "start failed");
                v->stop(ctx);
                v->destroy(ctx);
                return 1;
            }
            *handle = ctx;
            (void)e;
            return 0;
        };
        p.poll = [v](void *handle, int, QString *err) {
            PsError pe{};
            if (!v->poll(handle, &pe)) {
                if (err) *err = QString::fromUtf8(pe.message[0] ? pe.message : "poll failed");
                return 1;
            }
            return 0;
        };
        p.close = [v](void *handle) {
            if (!handle) return;
            v->stop(handle);
            v->destroy(handle);
        };
        p.status = [v](void *handle) {
            if (v->status_json)
            {
                char buf[2048] = {0};
                const int32_t n = v->status_json(handle, buf, sizeof(buf));
                if (n >= 0)
                    return QString::fromUtf8(buf, n);
            }
            const PsInputState st = handle ? v->current_state(handle) : PS_INPUT_IDLE;
            const char *s = st == PS_INPUT_RUNNING ? "running"
                           : st == PS_INPUT_DONE ? "done"
                           : st == PS_INPUT_ERROR ? "error"
                           : st == PS_INPUT_PAUSED ? "paused" : "idle";
            return QStringLiteral("{\"state\":\"%1\"}").arg(QString::fromLatin1(s));
        };
        if (v->send_command)
            p.send = [v](void *handle, const QString &command) {
                PsStringView out{};
                PsError pe{};
                const QByteArray cmdBytes = command.toUtf8();
                PsStringView cmd{cmdBytes.constData(), static_cast<uint32_t>(cmdBytes.size())};
                if (!v->send_command(handle, cmd, &out, &pe))
                    return QString::fromUtf8(pe.message[0] ? pe.message : "send failed");
                return QString::fromUtf8(out.data ? out.data : "{}", static_cast<int>(out.size));
            };
        m_streamPlugins.append(p);
        return true;
    }

    if (desc->family == PS_FAMILY_DATA_SOURCE)
    {
        const PsDataSourceVtable *v = desc->source;
        if (!v || v->abi_version != PS_PLUGIN_ABI_VERSION || !v->id || !v->id[0] || !v->open ||
            !v->poll || !v->close)
            return false;
        const QString id = QString::fromUtf8(v->id);
        if (findSourceById(id))
            return false;

        DataInputPlugin p;
        p.id = id;
        p.version = manifestVersion(desc);
        p.label = QString::fromUtf8(v->label ? v->label : v->id);
        p.kind = QString::fromUtf8(v->kind ? v->kind : "source");
        p.transport = QString::fromUtf8(v->transport ? v->transport : "internal");
        p.recordable = v->recordable != 0;
        p.replayable = (desc->capabilities & PS_CAP_REPLAY) != 0;
        p.capabilities = desc->capabilities;
        const QJsonObject manifest = QJsonDocument::fromJson(QByteArray(desc->manifest_json)).object();
        p.replayExtensions = manifest.value(QStringLiteral("replay_extensions")).toString();
        p.schema = QJsonDocument::fromJson(readJson(v->config_schema_json).toUtf8()).object();
        p.defaultConfig = [v] { return readJson(v->default_config_json); };
        p.open = [v](const QString &config, const PsSourceSink *sink, void **handle, QString *err) {
            char e[1024] = {0};
            const int rc = v->open(config.toUtf8().constData(), sink, ps_host_api(), handle, e, sizeof(e));
            if (rc != 0 && err)
                *err = QString::fromUtf8(e[0] ? e : "open failed");
            return rc;
        };
        p.poll = [v](void *handle, int timeoutMs, QString *err) {
            char e[1024] = {0};
            const int rc = v->poll(handle, timeoutMs, e, sizeof(e));
            if (rc != 0 && err)
                *err = QString::fromUtf8(e[0] ? e : "poll failed");
            return rc;
        };
        p.close = [v](void *handle) { v->close(handle); };
        p.status = [v](void *handle) {
            return readJson([v, handle](char *o, uint32_t c) {
                return v->status_json ? v->status_json(handle, o, c) : -1;
            });
        };
        if (v->send_command)
            p.send = [v](void *handle, const QString &command) {
                return readJson([v, handle, command](char *o, uint32_t c) {
                    return v->send_command(handle, command.toUtf8().constData(), o, c);
                });
            };
        m_streamPlugins.append(p);
        return true;
    }

    return false;
}

QStringList DataInputRegistry::scanPlugins(const QStringList &dirs)
{
    PluginRuntime &rt = PluginRuntime::instance();
    QStringList errors;
    for (const QString &dir : dirs)
        rt.scanDirectory(dir.toStdString());

    for (const PsLoadedPlugin &p : rt.plugins())
    {
        if (!p.descriptor)
            continue;
        if (p.descriptor->family != PS_FAMILY_DATA_LOADER &&
            p.descriptor->family != PS_FAMILY_DATA_SOURCE &&
            p.descriptor->family != PS_FAMILY_DATA_INPUT)
            continue;
        if (!registerDescriptor(p.descriptor, QString::fromStdString(p.packageDir)))
            errors << QStringLiteral("%1：数据输入插件注册失败").arg(QString::fromStdString(p.library));
    }
    for (const std::string &e : rt.errors())
        errors << QString::fromStdString(e);
    return errors;
}

const DataInputPlugin *DataInputRegistry::findFileBySuffix(const QString &suffix) const
{
    const QString dot = suffix.startsWith('.') ? suffix.toLower() : QStringLiteral(".") + suffix.toLower();
    for (const DataInputPlugin &p : m_filePlugins)
        if (p.extensions.contains(dot, Qt::CaseInsensitive))
            return &p;
    // 未知扩展名兜底给 CSV；解析失败由插件报错。
    for (const DataInputPlugin &p : m_filePlugins)
        if (p.id == QLatin1String("data_loader_csv"))
            return &p;
    return nullptr;
}

const DataInputPlugin *DataInputRegistry::findFileById(const QString &id) const
{
    for (const DataInputPlugin &p : m_filePlugins)
        if (p.id == id)
            return &p;
    return nullptr;
}

const DataInputPlugin *DataInputRegistry::findSourceById(const QString &id) const
{
    for (const DataInputPlugin &p : m_streamPlugins)
        if (p.id == id)
            return &p;
    return nullptr;
}
