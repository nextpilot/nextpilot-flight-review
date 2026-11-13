#include "app/cache/DatasetCache.h"

#include "ps_kernel.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QVector>

namespace {

constexpr quint32 kParamsMagic = 0x50535031u; // PSP1
constexpr quint32 kParamsVersion = 1u;
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
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QDataStream in(&f);
    in.setByteOrder(QDataStream::LittleEndian);
    in.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0, version = 0;
    qint64 size = 0, mtime = 0;
    QString json;
    in >> magic >> version >> size >> mtime >> json;
    if (in.status() != QDataStream::Ok || magic != kParamsMagic || version != kParamsVersion ||
        size != source.size() || mtime != source.lastModified().toMSecsSinceEpoch() ||
        json.isEmpty() || json.size() > 4 * 1024 * 1024)
    {
        f.close();
        QFile::remove(path);
        return {};
    }
    return json;
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
    QSaveFile f(paramsCachePath(sourcePath, loaderId, opts));
    if (!f.open(QIODevice::WriteOnly))
        return false;
    QDataStream out(&f);
    out.setByteOrder(QDataStream::LittleEndian);
    out.setVersion(QDataStream::Qt_6_0);
    out << kParamsMagic << kParamsVersion << qint64(source.size())
        << qint64(source.lastModified().toMSecsSinceEpoch()) << json;
    if (out.status() != QDataStream::Ok || !f.commit())
    {
        f.cancelWriting();
        return false;
    }
    return true;
}

void DatasetCache::clear()
{
    QDir(cacheDir()).removeRecursively();
}
