#include "DataSourceConfig.h"

#include <QJsonValue>

namespace ps::data_source
{

QJsonObject DataSourceConfig::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("schemaVersion")] = kSchemaVersion;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("alias")] = alias;
    o[QStringLiteral("path")] = path;
    o[QStringLiteral("fmt")] = fmt;
    o[QStringLiteral("sourceKey")] = sourceKey;
    o[QStringLiteral("offsetSec")] = offsetSec;
    o[QStringLiteral("mode")] = int(mode);
    o[QStringLiteral("extensionId")] = extensionId;
    o[QStringLiteral("configJson")] = configJson;
    return o;
}

DataSourceConfig DataSourceConfig::fromJson(const QJsonObject &o)
{
    DataSourceConfig c;
    // 只接受不高于当前版本的配置；旧配置缺省按 v1 读。
    c.schemaVersion = o.value(QStringLiteral("schemaVersion")).toInt(kSchemaVersion);
    c.id = o.value(QStringLiteral("id")).toInt(-1);
    c.name = o.value(QStringLiteral("name")).toString();
    c.alias = o.value(QStringLiteral("alias")).toString();
    c.path = o.value(QStringLiteral("path")).toString();
    c.fmt = o.value(QStringLiteral("fmt")).toString();
    c.sourceKey = o.value(QStringLiteral("sourceKey")).toString();
    c.offsetSec = o.value(QStringLiteral("offsetSec")).toDouble(0.0);
    c.mode = static_cast<DataSourceMode>(o.value(QStringLiteral("mode")).toInt(0));
    c.extensionId = o.value(QStringLiteral("extensionId")).toString();
    c.configJson = o.value(QStringLiteral("configJson")).toString();
    return c;
}

} // namespace ps::data_source
