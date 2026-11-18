#include "UlogFormatDriver.h"

#include "DataRepository.h"
#include "Dataset.h"
#include "UlogStreamParser.h"

#include <QFileInfo>

using namespace ng::core;

QString UlogFormatDriver::id() const
{
    return QStringLiteral("ulog");
}

QString UlogFormatDriver::displayName() const
{
    return QStringLiteral("ULog 格式驱动");
}

ng::core::FormatDomain UlogFormatDriver::domain() const
{
    return ng::core::FormatDomain::Autopilot;
}

QStringList UlogFormatDriver::suffixes() const
{
    return {QStringLiteral("ulg"), QStringLiteral("ulog")};
}

bool UlogFormatDriver::acceptsFormat(const QString &format) const
{
    return format == QStringLiteral("ulog") || format == QStringLiteral(".ulg");
}

// 加载外壳：缓存优先 -> 流式解析 -> 写缓存 -> 入仓库。
// 与旧 UlogExtension::Handler 完全同构，只是入口从 IIntentHandler 换成 IFormatDriver，
// format 归属判断上移到 ExtensionRegistry::driverForFormat。
Result<IntentResult> UlogFormatDriver::load(const Intent &intent, DataRepository &repo)
{
    const QString path = intent.params.value(QStringLiteral("path")).toString();
    const QFileInfo fi(path);
    if (!fi.exists())
        return fail(QStringLiteral("eio"), QStringLiteral("文件不存在: %1").arg(path));

    const qint64 sourceSize = fi.size();
    const qint64 sourceMtime = fi.lastModified().toMSecsSinceEpoch();
    const QString cachePath = path + QStringLiteral(".psc");

    Dataset ds;
    bool fromCache = false;
    if (!intent.params.value(QStringLiteral("nocache"), false).toBool())
    {
        QString cacheErr;
        if (auto cached = Dataset::loadCache(cachePath, sourceSize, sourceMtime, &cacheErr))
        {
            ds = std::move(*cached);
            ds.setFormat(QStringLiteral("ulog"));
            fromCache = true;
        }
    }

    if (!fromCache)
    {
        ds = Dataset::create(fi.completeBaseName(), QStringLiteral("ulog"));
        QString parseErr;
        if (!ng::ext::ulog::parseUlogStream(path, ds, parseErr))
            return fail(QStringLiteral("eformat"),
                        QStringLiteral("ULog 解析失败: %1").arg(parseErr));
        ds.refreshSchema();
        QString cacheErr;
        ds.saveCache(cachePath, sourceSize, sourceMtime, &cacheErr);
    }

    DatasetId datasetId;
    if (intent.params.contains(QStringLiteral("id")))
    {
        const DatasetId wanted = intent.params.value(QStringLiteral("id")).toInt();
        Result<DatasetId> added = repo.addWithStableId(std::move(ds), wanted);
        if (!added)
            return fail(added.error().code, added.error().message);
        datasetId = added.value();
    }
    else
    {
        datasetId = repo.add(std::move(ds));
    }
    IntentResult r;
    r.payload = datasetId;
    return r;
}
