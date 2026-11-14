#include "PluginComponent.h"

#include "ps_plugin_api.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstring>

namespace ps::components
{
namespace
{

QVector<RibbonContribution> parseRibbon(const QString &json)
{
    QVector<RibbonContribution> out;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    QJsonArray pages;
    if (doc.isArray())
        pages = doc.array();
    else if (doc.isObject())
    {
        const QJsonObject root = doc.object();
        if (root.contains(QStringLiteral("pages")))
            pages = root.value(QStringLiteral("pages")).toArray();
        else
            pages.append(root);
    }

    for (const QJsonValue &pv : pages)
    {
        const QJsonObject po = pv.toObject();
        RibbonContribution page;
        page.page = po.value(QStringLiteral("page")).toString();
        page.panel = po.value(QStringLiteral("panel")).toString(QStringLiteral("命令"));
        for (const QJsonValue &iv : po.value(QStringLiteral("items")).toArray())
        {
            const QJsonObject io = iv.toObject();
            RibbonItem item;
            item.action = io.value(QStringLiteral("action")).toString();
            item.label = io.value(QStringLiteral("label")).toString(item.action);
            item.large = io.value(QStringLiteral("large")).toBool(true);
            if (!item.action.isEmpty())
                page.items.push_back(item);
        }
        if (!page.page.isEmpty() && !page.items.isEmpty())
            out.push_back(page);
    }
    return out;
}

} // namespace

ComponentManifest PluginComponent::manifest() const
{
    ComponentManifest m;
    if (!m_descriptor)
        return m;
    const QJsonObject obj = QJsonDocument::fromJson(QByteArray(m_descriptor->manifest_json ? m_descriptor->manifest_json : "")).object();
    m.id = obj.value(QStringLiteral("id")).toString();
    m.name = obj.value(QStringLiteral("name")).toString(m.id);
    m.version = obj.value(QStringLiteral("version")).toString();
    if (m_descriptor->protocol)
        m.capabilities << QStringLiteral("protocol");
    if (m_descriptor->loader)
        m.capabilities << QStringLiteral("loader");
    if (m_descriptor->ui && m_descriptor->ui->ribbon_json)
        m.capabilities << QStringLiteral("ribbon");
    return m;
}

void PluginComponent::contribute(Registry &registry, const HostServices &host)
{
    if (!m_descriptor || m_descriptor->abi_version != PS_PLUGIN_ABI_VERSION)
    {
        if (host.log)
            host.log(QStringLiteral("插件组件 ABI 不匹配，跳过 UI 贡献"));
        return;
    }
    const PsProtocolVtable *protocol = m_descriptor->protocol;
    if (protocol && protocol->abi_version == PS_PLUGIN_ABI_VERSION && protocol->kind)
    {
        ProtocolContribution p;
        p.kind = QString::fromUtf8(protocol->kind);
        p.label = QString::fromUtf8(protocol->label ? protocol->label : protocol->kind);
        p.schema = QJsonDocument::fromJson(
                       QByteArray(protocol->schema_json ? protocol->schema_json : "")).object();
        if (protocol->validate)
        {
            p.validate = [protocol](const QJsonObject &obj) {
                const QByteArray json = QJsonDocument(obj).toJson(QJsonDocument::Compact);
                char err[4096] = {0};
                const int32_t n = protocol->validate(json.constData(), err, sizeof(err));
                if (n <= 0)
                    return QStringList();
                return QString::fromUtf8(err).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            };
        }
        registry.addProtocol(p);
    }

    const PsUiVtable *ui = m_descriptor->ui;
    if (!ui || !ui->ribbon_json || !ui->ribbon_json[0])
        return;

    const auto pages = parseRibbon(QString::fromUtf8(ui->ribbon_json));
    for (const RibbonContribution &page : pages)
        registry.addRibbon(page);

    if (!ui->on_event)
        return;

    for (const RibbonContribution &page : pages)
    {
        for (const RibbonItem &item : page.items)
        {
            const QString action = item.action;
            registry.addAction(action, [ui, action](const ActionContext &) {
                char out[512] = {0};
                ui->on_event(action.toUtf8().constData(), "{}", out, sizeof(out));
            });
        }
    }
}

} // namespace ps::components
