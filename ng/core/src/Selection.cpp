#include "Selection.h"

#include <QJsonArray>

namespace nextpilot::plotscope::core
{

QString selectionKindName(SelectionKind kind)
{
    switch (kind)
    {
    case SelectionKind::None:          return QStringLiteral("none");
    case SelectionKind::Source:        return QStringLiteral("source");
    case SelectionKind::Dataset:       return QStringLiteral("dataset");
    case SelectionKind::Table:         return QStringLiteral("table");
    case SelectionKind::Column:        return QStringLiteral("column");
    case SelectionKind::Param:         return QStringLiteral("param");
    case SelectionKind::Message:       return QStringLiteral("message");
    case SelectionKind::Series:        return QStringLiteral("series");
    case SelectionKind::Axis:          return QStringLiteral("axis");
    case SelectionKind::ProtocolField: return QStringLiteral("protocolField");
    case SelectionKind::Canvas:        return QStringLiteral("canvas");
    }
    return QStringLiteral("none");
}

SelectionKind selectionKindFromName(const QString &name)
{
    if (name == QStringLiteral("source"))        return SelectionKind::Source;
    if (name == QStringLiteral("dataset"))       return SelectionKind::Dataset;
    if (name == QStringLiteral("table"))         return SelectionKind::Table;
    if (name == QStringLiteral("column"))        return SelectionKind::Column;
    if (name == QStringLiteral("param"))         return SelectionKind::Param;
    if (name == QStringLiteral("message"))       return SelectionKind::Message;
    if (name == QStringLiteral("series"))        return SelectionKind::Series;
    if (name == QStringLiteral("axis"))          return SelectionKind::Axis;
    if (name == QStringLiteral("protocolField")) return SelectionKind::ProtocolField;
    if (name == QStringLiteral("canvas"))        return SelectionKind::Canvas;
    return SelectionKind::None;
}

void Selection::clear()
{
    kind = SelectionKind::None;
    ids.clear();
    label.clear();
    icon.clear();
    extra.clear();
}

QString Selection::key() const
{
    if (empty())
        return QString();
    QStringList parts;
    parts << selectionKindName(kind);
    // ids 按键排序，保证 key 稳定（QVariantMap 本身有序，但显式排序更稳）。
    QStringList keys = ids.keys();
    keys.sort(Qt::CaseSensitive);
    for (const QString &k : keys)
        parts << k + QLatin1Char('=') + ids.value(k).toString();
    return parts.join(QLatin1Char('|'));
}

QJsonObject Selection::toJson() const
{
    QJsonObject idsObj = QJsonObject::fromVariantMap(ids);
    QJsonObject extraObj = QJsonObject::fromVariantMap(extra);
    return QJsonObject{
        {QStringLiteral("kind"), selectionKindName(kind)},
        {QStringLiteral("label"), label},
        {QStringLiteral("icon"), icon},
        {QStringLiteral("ids"), idsObj},
        {QStringLiteral("extra"), extraObj},
    };
}

Selection Selection::fromJson(const QJsonObject &obj)
{
    Selection s;
    s.kind = selectionKindFromName(obj.value(QStringLiteral("kind")).toString());
    s.label = obj.value(QStringLiteral("label")).toString();
    s.icon = obj.value(QStringLiteral("icon")).toString();
    s.ids = obj.value(QStringLiteral("ids")).toObject().toVariantMap();
    s.extra = obj.value(QStringLiteral("extra")).toObject().toVariantMap();
    return s;
}

} // namespace nextpilot::plotscope::core
