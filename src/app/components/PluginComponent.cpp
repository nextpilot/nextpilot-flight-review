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

void PluginComponent::contribute(Registry &registry, const HostServices &host)
{
    if (!m_descriptor || m_descriptor->abi_version != PS_PLUGIN_ABI_VERSION)
    {
        if (host.log)
            host.log(QStringLiteral("插件组件 ABI 不匹配，跳过 UI 贡献"));
        return;
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
