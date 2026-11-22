#include "WorkspaceDataConfig.h"

#include "SettingsStore.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

using namespace nextpilot::plotscope::context;

namespace
{
// 把 QStringList 与 QJsonArray 互转（解析器 id 列表）。
QStringList stringListFromJson(const QJsonArray &arr)
{
    QStringList out;
    for (const QJsonValue &v : arr)
        if (v.isString())
            out << v.toString();
    return out;
}

QJsonArray stringListToJson(const QStringList &list)
{
    QJsonArray arr;
    for (const QString &s : list)
        arr.append(s);
    return arr;
}
} // namespace

// ---------------------------------------------------------------------------
// DeriveRecipeSpec
// ---------------------------------------------------------------------------

QJsonObject DeriveRecipeSpec::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("sourceOrigin")] = sourceOrigin;
    o[QStringLiteral("table")] = table;
    o[QStringLiteral("column")] = column;
    o[QStringLiteral("filter")] = filter;
    o[QStringLiteral("paramsJson")] = paramsJson;
    o[QStringLiteral("name")] = name;
    return o;
}

DeriveRecipeSpec DeriveRecipeSpec::fromJson(const QJsonObject &o)
{
    DeriveRecipeSpec r;
    r.sourceOrigin = o.value(QStringLiteral("sourceOrigin")).toString();
    r.table = o.value(QStringLiteral("table")).toString();
    r.column = o.value(QStringLiteral("column")).toString();
    r.filter = o.value(QStringLiteral("filter")).toString();
    r.paramsJson = o.value(QStringLiteral("paramsJson")).toString();
    r.name = o.value(QStringLiteral("name")).toString();
    return r;
}

bool DeriveRecipeSpec::isValid() const
{
    // sourceOrigin 可空（表示"当前活动源"，脚本场景）；但表/列/算子必须齐。
    return !table.isEmpty() && !column.isEmpty() && !filter.isEmpty();
}

// ---------------------------------------------------------------------------
// SourceSpec
// ---------------------------------------------------------------------------

QJsonObject SourceSpec::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("schemaVersion")] = schemaVersion;
    o[QStringLiteral("origin")] = origin;
    o[QStringLiteral("kind")] = kind;

    o[QStringLiteral("parserIds")] = stringListToJson(parserIds);
    o[QStringLiteral("parserParams")] = parserParams; // 本身就是 QJsonObject
    o[QStringLiteral("topicFilter")] = topicFilter;

    o[QStringLiteral("recordings")] = recordings;
    o[QStringLiteral("timeOffsetSec")] = timeOffsetSec;

    QJsonArray recipes;
    for (const DeriveRecipeSpec &r : deriveRecipes)
        recipes.append(r.toJson());
    o[QStringLiteral("deriveRecipes")] = recipes;
    return o;
}

SourceSpec SourceSpec::fromJson(const QJsonObject &o)
{
    SourceSpec s;
    s.schemaVersion = o.value(QStringLiteral("schemaVersion")).toInt(1);
    s.origin = o.value(QStringLiteral("origin")).toString();
    s.kind = o.value(QStringLiteral("kind")).toString(QStringLiteral("file"));

    s.parserIds = stringListFromJson(o.value(QStringLiteral("parserIds")).toArray());
    s.parserParams = o.value(QStringLiteral("parserParams")).toObject();
    s.topicFilter = o.value(QStringLiteral("topicFilter")).toString();

    s.recordings = o.value(QStringLiteral("recordings")).toArray();
    s.timeOffsetSec = o.value(QStringLiteral("timeOffsetSec")).toDouble(0.0);

    const QJsonArray recipes = o.value(QStringLiteral("deriveRecipes")).toArray();
    for (const QJsonValue &v : recipes)
    {
        if (!v.isObject())
            continue;
        DeriveRecipeSpec r = DeriveRecipeSpec::fromJson(v.toObject());
        if (r.isValid())
            s.deriveRecipes << r;
    }
    return s;
}

bool SourceSpec::isValid() const
{
    return !origin.isEmpty();
}

// ---------------------------------------------------------------------------
// WorkspaceDataConfig
// ---------------------------------------------------------------------------

QJsonObject WorkspaceDataConfig::toJson() const
{
    QJsonArray arr;
    for (const SourceSpec &s : sources)
        arr.append(s.toJson());

    QJsonObject o;
    o[QStringLiteral("schemaVersion")] = kSchemaVersion;
    o[QStringLiteral("sources")] = arr;
    return o;
}

WorkspaceDataConfig WorkspaceDataConfig::fromJson(const QJsonObject &o)
{
    WorkspaceDataConfig cfg;
    const QJsonArray arr = o.value(QStringLiteral("sources")).toArray();
    for (const QJsonValue &v : arr)
    {
        if (!v.isObject())
            continue;
        SourceSpec s = SourceSpec::fromJson(v.toObject());
        if (s.isValid())
            cfg.sources << s;
    }
    return cfg;
}

WorkspaceDataConfig WorkspaceDataConfig::load(const SettingsStore &store)
{
    return fromJson(store.json(SettingsScope::Workspace, QString::fromLatin1(kSettingsKey)));
}

bool WorkspaceDataConfig::save(SettingsStore &store, QString *) const
{
    return store.setJson(SettingsScope::Workspace, QString::fromLatin1(kSettingsKey), toJson());
}

const SourceSpec *WorkspaceDataConfig::findByOrigin(const QString &origin) const
{
    for (const SourceSpec &s : sources)
        if (s.origin == origin)
            return &s;
    return nullptr;
}

SourceSpec *WorkspaceDataConfig::findByOrigin(const QString &origin)
{
    for (SourceSpec &s : sources)
        if (s.origin == origin)
            return &s;
    return nullptr;
}

void WorkspaceDataConfig::upsert(const SourceSpec &spec)
{
    if (!spec.isValid())
        return;
    if (SourceSpec *existing = findByOrigin(spec.origin))
    {
        *existing = spec;
        return;
    }
    sources << spec;
}

bool WorkspaceDataConfig::removeByOrigin(const QString &origin)
{
    for (int i = 0; i < sources.size(); ++i)
    {
        if (sources.at(i).origin == origin)
        {
            sources.remove(i);
            return true;
        }
    }
    return false;
}
