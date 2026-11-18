#include "PropertySchema.h"

#include <QJsonArray>

namespace ng::core
{

namespace
{
QString kindToString(PropKind k)
{
    switch (k)
    {
    case PropKind::Text: return QStringLiteral("text");
    case PropKind::Number: return QStringLiteral("number");
    case PropKind::Bool: return QStringLiteral("bool");
    case PropKind::Enum: return QStringLiteral("enum");
    case PropKind::Color: return QStringLiteral("color");
    case PropKind::ReadOnly: return QStringLiteral("readonly");
    }
    return QStringLiteral("readonly");
}

PropKind stringToKind(const QString &s)
{
    if (s == QStringLiteral("text")) return PropKind::Text;
    if (s == QStringLiteral("number")) return PropKind::Number;
    if (s == QStringLiteral("bool")) return PropKind::Bool;
    if (s == QStringLiteral("enum")) return PropKind::Enum;
    if (s == QStringLiteral("color")) return PropKind::Color;
    return PropKind::ReadOnly;
}
} // namespace

const PropObject *PropertySchema::object(const QString &id) const
{
    for (const PropObject &o : objects)
        if (o.id == id)
            return &o;
    return nullptr;
}

QJsonObject PropertySchema::toJson() const
{
    QJsonArray objs;
    for (const PropObject &o : objects)
    {
        QJsonArray rows;
        for (const PropRow &r : o.rows)
        {
            QJsonArray options;
            for (const QString &opt : r.options)
                options.append(opt);
            rows.append(QJsonObject{
                {QStringLiteral("key"), r.key},
                {QStringLiteral("label"), r.label},
                {QStringLiteral("value"), r.value},
                {QStringLiteral("kind"), kindToString(r.kind)},
                {QStringLiteral("editable"), r.editable},
                {QStringLiteral("unit"), r.unit},
                {QStringLiteral("options"), options},
                {QStringLiteral("tooltip"), r.tooltip},
            });
        }
        objs.append(QJsonObject{
            {QStringLiteral("id"), o.id},
            {QStringLiteral("parentId"), o.parentId},
            {QStringLiteral("label"), o.label},
            {QStringLiteral("icon"), o.icon},
            {QStringLiteral("rows"), rows},
        });
    }
    return QJsonObject{
        {QStringLiteral("title"), title},
        {QStringLiteral("objects"), objs},
        {QStringLiteral("selectedId"), selectedId},
    };
}

PropertySchema PropertySchema::fromJson(const QJsonObject &obj)
{
    PropertySchema s;
    s.title = obj.value(QStringLiteral("title")).toString();
    s.selectedId = obj.value(QStringLiteral("selectedId")).toString();
    const QJsonArray objs = obj.value(QStringLiteral("objects")).toArray();
    for (const QJsonValue &ov : objs)
    {
        const QJsonObject oo = ov.toObject();
        PropObject o;
        o.id = oo.value(QStringLiteral("id")).toString();
        o.parentId = oo.value(QStringLiteral("parentId")).toString();
        o.label = oo.value(QStringLiteral("label")).toString();
        o.icon = oo.value(QStringLiteral("icon")).toString();
        const QJsonArray rows = oo.value(QStringLiteral("rows")).toArray();
        for (const QJsonValue &rv : rows)
        {
            const QJsonObject ro = rv.toObject();
            PropRow r;
            r.key = ro.value(QStringLiteral("key")).toString();
            r.label = ro.value(QStringLiteral("label")).toString();
            r.value = ro.value(QStringLiteral("value")).toString();
            r.kind = stringToKind(ro.value(QStringLiteral("kind")).toString());
            r.editable = ro.value(QStringLiteral("editable")).toBool();
            r.unit = ro.value(QStringLiteral("unit")).toString();
            r.tooltip = ro.value(QStringLiteral("tooltip")).toString();
            const QJsonArray opts = ro.value(QStringLiteral("options")).toArray();
            for (const QJsonValue &opt : opts)
                r.options.append(opt.toString());
            o.rows.append(r);
        }
        s.objects.append(o);
    }
    return s;
}

} // namespace ng::core
