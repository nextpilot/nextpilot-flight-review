#include "WorkspacePreset.h"

#include <QJsonValue>

namespace nextpilot::plotscope::context
{

namespace
{

// parserParams 是"按 parserId 分组的参数"，generic 模式下整块丢弃：
// 保留它但没有对应解析器，等于给恢复流程埋一颗必炸的雷。
const char *const kParserParamsKey = "parserParams";
// 条目级可选键：相对锚定与内容校验（同名源防串）。
const char *const kOriginResolvedKey = "originResolved";
const char *const kContentSha256Key = "contentSha256";
// generic 保留的定位字段。
const char *const kKeepKeys[] = {"schemaVersion", "origin", "kind", "formatHint", "parserIds",
                                 "topicFilter", "timeOffsetSec", "originResolved",
                                 "contentSha256"};

} // namespace

QJsonObject WorkspacePreset::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("version"), version);
    o.insert(QStringLiteral("kind"), kind);
    o.insert(QStringLiteral("name"), name);
    o.insert(QStringLiteral("savedAt"), savedAt);
    o.insert(QStringLiteral("sourceBinding"), sourceBinding);
    o.insert(QStringLiteral("sources"), sources);
    o.insert(QStringLiteral("plots"), plots);
    return o;
}

WorkspacePreset WorkspacePreset::fromJson(const QJsonObject &obj, QString *error)
{
    WorkspacePreset p;
    const int version = obj.value(QStringLiteral("version")).toInt(1);
    if (version > kSchemaVersion)
    {
        if (error != nullptr)
            *error = QStringLiteral("预设版本 %1 高于本端支持的 %2：打开会丢失新增字段，"
                                    "请升级后再试")
                         .arg(version)
                         .arg(kSchemaVersion);
        return p;
    }
    p.version = version;
    p.kind = obj.value(QStringLiteral("kind")).toString(QStringLiteral("preset"));
    p.name = obj.value(QStringLiteral("name")).toString();
    p.savedAt = obj.value(QStringLiteral("savedAt")).toString();
    p.sourceBinding = obj.value(QStringLiteral("sourceBinding")).toString(QStringLiteral("bound"));
    p.sources = obj.value(QStringLiteral("sources")).toArray();
    p.plots = obj.value(QStringLiteral("plots")).toObject();
    return p;
}

QJsonObject WorkspacePreset::genericSourceEntry(const QJsonObject &source)
{
    QJsonObject out;
    for (const char *key : kKeepKeys)
    {
        const QString k = QString::fromLatin1(key);
        if (source.contains(k))
            out.insert(k, source.value(k));
    }
    return out;
}

void WorkspacePreset::makeGeneric()
{
    sourceBinding = QStringLiteral("generic");
    QJsonArray out;
    for (const QJsonValue &v : sources)
        out.append(genericSourceEntry(v.toObject()));
    sources = out;
}

} // namespace nextpilot::plotscope::context
