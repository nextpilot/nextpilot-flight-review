#include "DatasetCache.h"

#include "ps_kernel.h"

#include <QCryptographicHash>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QVector>

namespace {

constexpr quint64 kMaxCacheBytes = 4ull * 1024 * 1024 * 1024;

} // namespace

QString DatasetCache::cacheDir()
{
    const QString override = qEnvironmentVariable("PLOTSCOPE_CACHE_DIR");
    const QString base = override.isEmpty()
                             ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                             : override;
    return QDir(base).filePath(QStringLiteral("datasets"));
}

namespace {

QString keyHash(const QString &sourcePath, const QString &loaderId, const QString &opts)
{
    const QString key = QDir::cleanPath(QFileInfo(sourcePath).absoluteFilePath()) + QLatin1Char('|') +
                        loaderId + QLatin1Char('|') + opts;
    return QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString paramsCachePath(const QString &sourcePath, const QString &loaderId, const QString &opts)
{
    return QDir(DatasetCache::cacheDir()).filePath(keyHash(sourcePath, loaderId, opts) +
                                                  QStringLiteral(".psparams"));
}

} // namespace

QString DatasetCache::cachePath(const QString &sourcePath, const QString &loaderId,
                                const QString &opts)
{
    return QDir(cacheDir()).filePath(keyHash(sourcePath, loaderId, opts) + QStringLiteral(".pscache"));
}

PsDataset *DatasetCache::load(const QString &sourcePath, const QString &loaderId,
                              const QString &opts)
{
    const QFileInfo source(sourcePath);
    if (!source.exists() || !source.isFile())
        return nullptr;

    const QString path = cachePath(sourcePath, loaderId, opts);
    char err[256] = {0};
    PsDataset *ds = nullptr;
    const int32_t rc = ps_cache_load_dataset(path.toUtf8().constData(), source.size(),
                                             source.lastModified().toMSecsSinceEpoch(), &ds, err,
                                             sizeof(err));
    if (rc == 0)
        return ds;
    if (rc == 1)
    {
        QFile::remove(path);
        return nullptr;
    }
    QFile::remove(path);
    return nullptr;
}

bool DatasetCache::save(const QString &sourcePath, const QString &loaderId, const QString &opts,
                        const PsDataset *ds)
{
    if (!ds || !QFileInfo(sourcePath).isFile())
        return false;
    if (!QDir().mkpath(cacheDir()))
        return false;

    const QFileInfo source(sourcePath);
    const QString path = cachePath(sourcePath, loaderId, opts);
    char err[256] = {0};
    if (ps_cache_save_dataset(ds, path.toUtf8().constData(), source.size(),
                              source.lastModified().toMSecsSinceEpoch(), err, sizeof(err)) != 0)
        return false;

    // 简单容量上限：超过 4 GiB 从最旧的开始删。
    QDir dir(cacheDir());
    QFileInfoList files = dir.entryInfoList({QStringLiteral("*.pscache"), QStringLiteral("*.psparams")},
                                           QDir::Files, QDir::Time);
    quint64 total = 0;
    for (const QFileInfo &fi : files)
        total += quint64(fi.size());
    for (int i = files.size() - 1; i >= 0 && total > kMaxCacheBytes; --i)
    {
        total -= quint64(files[i].size());
        QFile::remove(files[i].absoluteFilePath());
    }
    return true;
}

QString DatasetCache::loadParams(const QString &sourcePath, const QString &loaderId,
                                 const QString &opts)
{
    const QFileInfo source(sourcePath);
    if (!source.exists() || !source.isFile())
        return {};
    const QString path = paramsCachePath(sourcePath, loaderId, opts);
    QByteArray buf(4 * 1024 * 1024 + 1, '\0');
    char err[256] = {0};
    const int32_t rc = ps_cache_load_params(path.toUtf8().constData(), source.size(),
                                            source.lastModified().toMSecsSinceEpoch(), buf.data(),
                                            static_cast<uint32_t>(buf.size()), err, sizeof(err));
    if (rc == 0)
        return QString::fromUtf8(buf.constData());
    QFile::remove(path);
    return {};
}

bool DatasetCache::saveParams(const QString &sourcePath, const QString &loaderId,
                              const QString &opts, const QString &json)
{
    if (json.isEmpty() || json.size() > 4 * 1024 * 1024)
        return false;
    const QFileInfo source(sourcePath);
    if (!source.exists() || !source.isFile())
        return false;
    if (!QDir().mkpath(cacheDir()))
        return false;
    char err[256] = {0};
    return ps_cache_save_params(paramsCachePath(sourcePath, loaderId, opts).toUtf8().constData(),
                                json.toUtf8().constData(), source.size(),
                                source.lastModified().toMSecsSinceEpoch(), err, sizeof(err)) == 0;
}

void DatasetCache::clear()
{
    QDir(cacheDir()).removeRecursively();
}
