#include "datastore/DatasetCache.h"

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

constexpr quint32 kMagic = 0x50534331u; // PSC1
constexpr quint32 kVersion = 1u;
constexpr quint64 kMaxCacheBytes = 4ull * 1024 * 1024 * 1024;
constexpr quint32 kMaxPoints = 50'000'000u;

} // namespace

QString DatasetCache::cacheDir()
{
    const QString override = qEnvironmentVariable("PLOTSCOPE_CACHE_DIR");
    const QString base = override.isEmpty()
                             ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                             : override;
    return QDir(base).filePath(QStringLiteral("datasets"));
}

QString DatasetCache::cachePath(const QString &sourcePath, const QString &loaderId,
                                const QString &opts)
{
    const QString key = QDir::cleanPath(QFileInfo(sourcePath).absoluteFilePath()) + QLatin1Char('|') +
                        loaderId + QLatin1Char('|') + opts;
    const QString hash =
        QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex());
    return QDir(cacheDir()).filePath(hash + QStringLiteral(".pscache"));
}

PsDataset *DatasetCache::load(const QString &sourcePath, const QString &loaderId,
                              const QString &opts)
{
    const QFileInfo source(sourcePath);
    if (!source.exists() || !source.isFile())
        return nullptr;

    const QString path = cachePath(sourcePath, loaderId, opts);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return nullptr;

    QDataStream in(&f);
    in.setByteOrder(QDataStream::LittleEndian);
    in.setVersion(QDataStream::Qt_6_0);

    quint32 magic = 0;
    quint32 version = 0;
    qint64 size = 0;
    qint64 mtime = 0;
    quint32 channels = 0;
    in >> magic >> version >> size >> mtime >> channels;
    const qint64 currentSize = source.size();
    const qint64 currentMtime = source.lastModified().toMSecsSinceEpoch();
    if (magic != kMagic || version != kVersion || size != currentSize || mtime != currentMtime ||
        channels == 0 || channels > 65536u)
    {
        f.close();
        QFile::remove(path);
        return nullptr;
    }

    PsDataset *ds = ps_dataset_new();
    if (!ds)
        return nullptr;

    bool ok = true;
    for (quint32 i = 0; i < channels && ok; ++i)
    {
        QString name;
        QString unit;
        quint32 count = 0;
        in >> name >> unit >> count;
        if (name.size() > 4096 || unit.size() > 1024 || count > kMaxPoints)
        {
            ok = false;
            break;
        }
        QVector<double> t(static_cast<int>(count));
        QVector<double> v(static_cast<int>(count));
        for (quint32 k = 0; k < count; ++k)
            in >> t[static_cast<int>(k)];
        for (quint32 k = 0; k < count; ++k)
            in >> v[static_cast<int>(k)];
        if (in.status() != QDataStream::Ok)
        {
            ok = false;
            break;
        }
        char err[160] = {0};
        if (ps_dataset_add_channel(ds, name.toUtf8().constData(), unit.toUtf8().constData(),
                                   t.constData(), v.constData(), count, err, sizeof(err)) != 0)
            ok = false;
    }

    if (!ok)
    {
        ps_dataset_free(ds);
        f.close();
        QFile::remove(path);
        return nullptr;
    }
    return ds;
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
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;

    QDataStream out(&f);
    out.setByteOrder(QDataStream::LittleEndian);
    out.setVersion(QDataStream::Qt_6_0);
    const quint32 channels = ps_dataset_channel_count(ds);
    out << kMagic << kVersion << qint64(source.size())
        << qint64(source.lastModified().toMSecsSinceEpoch()) << channels;

    for (quint32 i = 0; i < channels; ++i)
    {
        const PsChannelInfo *ch = ps_dataset_channel(ds, i);
        if (!ch)
            continue;
        out << QString::fromUtf8(ch->name) << QString::fromUtf8(ch->unit) << ch->point_count;
        for (quint32 k = 0; k < ch->point_count; ++k)
            out << ch->time[k];
        for (quint32 k = 0; k < ch->point_count; ++k)
            out << ch->value[k];
    }
    if (out.status() != QDataStream::Ok || !f.commit())
    {
        f.cancelWriting();
        return false;
    }

    // 简单容量上限：超过 4 GiB 从最旧的开始删。
    QDir dir(cacheDir());
    QFileInfoList files = dir.entryInfoList({QStringLiteral("*.pscache")}, QDir::Files, QDir::Time);
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

void DatasetCache::clear()
{
    QDir(cacheDir()).removeRecursively();
}
