#include "PlotLayout.h"

#include <QJsonArray>

namespace nextpilot::plotscope::core
{

namespace
{

QJsonObject canvasToJson(const CanvasSpec &c)
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), c.id);
    o.insert(QStringLiteral("viewType"), c.viewType);
    o.insert(QStringLiteral("title"), c.title);
    o.insert(QStringLiteral("datasetId"), c.datasetId);
    o.insert(QStringLiteral("channels"), QJsonArray::fromStringList(c.channels));
    if (!c.style.isEmpty())
        o.insert(QStringLiteral("style"), c.style);
    if (!c.state.isEmpty())
        o.insert(QStringLiteral("state"), c.state);
    return o;
}

CanvasSpec canvasFromJson(const QJsonObject &o)
{
    CanvasSpec c;
    c.id = o.value(QStringLiteral("id")).toString();
    c.viewType = o.value(QStringLiteral("viewType")).toString();
    c.title = o.value(QStringLiteral("title")).toString();
    c.datasetId = o.value(QStringLiteral("datasetId")).toString();
    const QJsonArray ch = o.value(QStringLiteral("channels")).toArray();
    for (const QJsonValue &v : ch)
        c.channels.append(v.toString());
    c.style = o.value(QStringLiteral("style")).toObject();
    c.state = o.value(QStringLiteral("state")).toObject();
    return c;
}

QJsonObject nodeToJson(const LayoutNode &n)
{
    QJsonObject o;
    if (n.isCanvas)
    {
        o.insert(QStringLiteral("kind"), QStringLiteral("canvas"));
        o.insert(QStringLiteral("canvas"), canvasToJson(n.canvas));
        return o;
    }
    o.insert(QStringLiteral("kind"), QStringLiteral("split"));
    o.insert(QStringLiteral("direction"), n.direction);
    QJsonArray kids;
    for (const LayoutNode &c : n.children)
        kids.append(nodeToJson(c));
    o.insert(QStringLiteral("children"), kids);
    return o;
}

LayoutNode nodeFromJson(const QJsonObject &o)
{
    LayoutNode n;
    if (o.value(QStringLiteral("kind")).toString() == QStringLiteral("canvas"))
    {
        n.isCanvas = true;
        n.canvas = canvasFromJson(o.value(QStringLiteral("canvas")).toObject());
        return n;
    }
    n.isCanvas = false;
    n.direction = o.value(QStringLiteral("direction")).toString();
    const QJsonArray kids = o.value(QStringLiteral("children")).toArray();
    for (const QJsonValue &v : kids)
        n.children.append(nodeFromJson(v.toObject()));
    return n;
}

} // namespace

QJsonObject PlotDocumentSpec::toJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), version);
    root.insert(QStringLiteral("name"), name);
    QJsonArray pageArray;
    for (const PlotPageSpec &p : pages)
    {
        QJsonObject po;
        po.insert(QStringLiteral("id"), p.id);
        po.insert(QStringLiteral("title"), p.title);
        po.insert(QStringLiteral("root"), nodeToJson(p.root));
        if (!p.pageState.isEmpty())
            po.insert(QStringLiteral("pageState"), p.pageState);
        pageArray.append(po);
    }
    root.insert(QStringLiteral("pages"), pageArray);
    if (!documentState.isEmpty())
        root.insert(QStringLiteral("documentState"), documentState);
    return root;
}

PlotDocumentSpec PlotDocumentSpec::fromJson(const QJsonObject &obj)
{
    PlotDocumentSpec spec;
    spec.version = obj.value(QStringLiteral("version")).toInt(1);
    spec.name = obj.value(QStringLiteral("name")).toString();
    spec.documentState = obj.value(QStringLiteral("documentState")).toObject();

    const QJsonArray pageArray = obj.value(QStringLiteral("pages")).toArray();
    for (const QJsonValue &v : pageArray)
    {
        const QJsonObject po = v.toObject();
        PlotPageSpec page;
        page.id = po.value(QStringLiteral("id")).toString();
        page.title = po.value(QStringLiteral("title")).toString();
        page.root = nodeFromJson(po.value(QStringLiteral("root")).toObject());
        page.pageState = po.value(QStringLiteral("pageState")).toObject();
        spec.pages.append(page);
    }
    return spec;
}

} // namespace nextpilot::plotscope::core
