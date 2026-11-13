#include "source/SourceRegistry.h"

#include "runtime/PluginRuntime.h"

#include <QBuffer>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

extern "C" const PsPluginDescriptor *ps_plugin_entry_data_source_sim(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_source_mavlink(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_source_oem7(void);

constexpr int kJsonCap = 64 * 1024;

QString readJson(const std::function<int32_t(char *, uint32_t)> &fn)
{
    if (!fn)
        return {};
    QByteArray buf(kJsonCap, '\0');
    const int32_t n = fn(buf.data(), static_cast<uint32_t>(buf.size()));
    if (n <= 0)
        return {};
    return QString::fromUtf8(buf.constData(), static_cast<int>(n));
}

} // namespace

SourceRegistry::SourceRegistry()
{
    registerDescriptor(ps_plugin_entry_data_source_sim());
    registerDescriptor(ps_plugin_entry_data_source_mavlink());
    registerDescriptor(ps_plugin_entry_data_source_oem7());
}

SourceRegistry &SourceRegistry::instance()
{
    static SourceRegistry reg;
    return reg;
}

bool SourceRegistry::registerDescriptor(const PsPluginDescriptor *desc, const QString &packageDir)
{
    Q_UNUSED(packageDir);
    if (!desc || desc->abi_version != PS_PLUGIN_ABI_VERSION || desc->family != PS_FAMILY_DATA_SOURCE ||
        !desc->source)
        return false;
    const PsDataSourceVtable *v = desc->source;
    if (v->abi_version != PS_PLUGIN_ABI_VERSION || !v->id || !v->id[0] || !v->open || !v->poll ||
        !v->close || findById(QString::fromUtf8(v->id)))
        return false;

    SourcePlugin p;
    p.id = QString::fromUtf8(v->id);
    const QJsonObject manifest = QJsonDocument::fromJson(QByteArray(desc->manifest_json)).object();
    p.version = manifest.value(QStringLiteral("version")).toString(QStringLiteral("0"));
    p.label = QString::fromUtf8(v->label ? v->label : v->id);
    p.kind = QString::fromUtf8(v->kind ? v->kind : "source");
    p.transport = QString::fromUtf8(v->transport ? v->transport : "internal");
    p.recordable = v->recordable != 0;
    p.capabilities = desc->capabilities;
    p.schema = QJsonDocument::fromJson(readJson(v->config_schema_json).toUtf8()).object();
    p.defaultConfig = [v] { return readJson(v->default_config_json); };
    p.open = [v](const QString &config, const PsSourceSink *sink, void **handle, QString *err) {
        char e[1024] = {0};
        const int rc = v->open(config.toUtf8().constData(), sink, handle, e, sizeof(e));
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
    p.status = [v](void *handle) { return readJson([v, handle](char *o, uint32_t c) {
        return v->status_json ? v->status_json(handle, o, c) : -1;
    }); };
    m_plugins.append(p);
    return true;
}

QStringList SourceRegistry::scanPlugins(const QStringList &dirs)
{
    PluginRuntime &rt = PluginRuntime::instance();
    QStringList errors;
    for (const QString &dir : dirs)
        rt.scanDirectory(dir.toStdString());
    for (const PsLoadedPlugin &p : rt.plugins())
    {
        if (!p.descriptor || p.descriptor->family != PS_FAMILY_DATA_SOURCE)
            continue;
        if (!registerDescriptor(p.descriptor, QString::fromStdString(p.packageDir)))
            errors << QStringLiteral("%1：data_source 注册失败").arg(QString::fromStdString(p.library));
    }
    for (const std::string &e : rt.errors())
        errors << QString::fromStdString(e);
    return errors;
}

const SourcePlugin *SourceRegistry::findById(const QString &id) const
{
    for (const SourcePlugin &p : m_plugins)
        if (p.id == id)
            return &p;
    return nullptr;
}
