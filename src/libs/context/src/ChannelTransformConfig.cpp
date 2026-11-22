#include "ChannelTransformConfig.h"
#include "SettingsStore.h"

#include <QJsonArray>

namespace nextpilot::plotscope::context
{

bool ChannelTransformSpec::isValid() const
{
    return !origin.isEmpty() && !table.isEmpty() && !column.isEmpty()
        && !transform.isEmpty() && transform.contains(QStringLiteral("descriptorId"));
}

QJsonObject ChannelTransformSpec::toJson() const
{
    return QJsonObject{
        { QStringLiteral("origin"), origin },
        { QStringLiteral("table"), table },
        { QStringLiteral("column"), column },
        { QStringLiteral("transform"), transform },
    };
}

ChannelTransformSpec ChannelTransformSpec::fromJson(const QJsonObject &o)
{
    ChannelTransformSpec s;
    s.origin = o.value(QStringLiteral("origin")).toString();
    s.table = o.value(QStringLiteral("table")).toString();
    s.column = o.value(QStringLiteral("column")).toString();
    s.transform = o.value(QStringLiteral("transform")).toObject();
    return s;
}

ChannelTransformConfig ChannelTransformConfig::load(const SettingsStore &store)
{
    return fromJson(store.json(SettingsScope::Workspace, QString::fromLatin1(kSettingsKey)));
}

bool ChannelTransformConfig::save(SettingsStore &store, QString *) const
{
    return store.setJson(SettingsScope::Workspace, QString::fromLatin1(kSettingsKey), toJson());
}

QJsonObject ChannelTransformConfig::toJson() const
{
    QJsonObject root;
    root[QStringLiteral("schemaVersion")] = 1;
    QJsonArray arr;
    for (const ChannelTransformSpec &s : specs)
        arr.append(s.toJson());
    root[QStringLiteral("transforms")] = arr;
    return root;
}

ChannelTransformConfig ChannelTransformConfig::fromJson(const QJsonObject &o)
{
    ChannelTransformConfig cfg;
    const QJsonArray arr = o.value(QStringLiteral("transforms")).toArray();
    for (const QJsonValue &v : arr)
    {
        ChannelTransformSpec s = ChannelTransformSpec::fromJson(v.toObject());
        if (s.isValid())
            cfg.specs.append(s);
    }
    return cfg;
}

const ChannelTransformSpec *ChannelTransformConfig::find(const QString &origin, const QString &table,
                                                         const QString &column) const
{
    for (const ChannelTransformSpec &s : specs)
    {
        if (s.origin == origin && s.table == table && s.column == column)
            return &s;
    }
    return nullptr;
}

void ChannelTransformConfig::upsert(const ChannelTransformSpec &spec)
{
    if (!spec.isValid())
        return;
    for (ChannelTransformSpec &s : specs)
    {
        if (s.origin == spec.origin && s.table == spec.table && s.column == spec.column)
        {
            s.transform = spec.transform;
            return;
        }
    }
    specs.append(spec);
}

bool ChannelTransformConfig::removeByChannel(const QString &origin, const QString &table,
                                             const QString &column)
{
    for (int i = 0; i < specs.size(); ++i)
    {
        if (specs.at(i).origin == origin && specs.at(i).table == table && specs.at(i).column == column)
        {
            specs.removeAt(i);
            return true;
        }
    }
    return false;
}

} // namespace nextpilot::plotscope::context
