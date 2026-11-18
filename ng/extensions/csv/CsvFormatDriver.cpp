#include "CsvFormatDriver.h"

#include "DataRepository.h"
#include "Dataset.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>

using namespace ng::core;

namespace
{
// 流式分块大小：C++ 侧缓冲恒定，与文件大小无关。
constexpr int kChunkRows = 4096;
} // namespace

QString CsvFormatDriver::id() const
{
    return QStringLiteral("csv");
}

QString CsvFormatDriver::displayName() const
{
    return QStringLiteral("CSV 格式驱动");
}

QStringList CsvFormatDriver::suffixes() const
{
    return {QStringLiteral("csv")};
}

bool CsvFormatDriver::acceptsFormat(const QString &format) const
{
    // 兼容默认：空 format 视为 csv（历史调用省略 format 时按 CSV 处理）。
    return format.isEmpty() || format == QStringLiteral("csv") || format == QStringLiteral(".csv");
}

Result<IntentResult> CsvFormatDriver::load(const Intent &intent, DataRepository &repo)
{
    const QString path = intent.params.value(QStringLiteral("path")).toString();
    const QFileInfo fi(path);
    if (!fi.exists())
        return fail(QStringLiteral("eio"), QStringLiteral("文件不存在: %1").arg(path));

    const QString tableName = intent.params.value(QStringLiteral("table"),
                                                  fi.completeBaseName()).toString();
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
            ds.setFormat(QStringLiteral("csv"));
            fromCache = true;
        }
    }

    if (!fromCache)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return fail(QStringLiteral("eio"), QStringLiteral("无法打开: %1").arg(path));
        QTextStream in(&file);

        if (in.atEnd())
            return fail(QStringLiteral("eempty"), QStringLiteral("文件为空"));
        const QString headerLine = in.readLine();
        QStringList headers = headerLine.split(QLatin1Char(','));
        for (QString &c : headers)
            c = c.trimmed();
        if (headers.isEmpty() || headers.first().isEmpty())
            return fail(QStringLiteral("eformat"), QStringLiteral("无表头"));

        const int valueColCount = headers.size() - 1;
        if (valueColCount <= 0)
            return fail(QStringLiteral("eformat"), QStringLiteral("无数据列"));

        QVector<TimeUs> timeBuf(kChunkRows);
        QVector<QVector<double>> valueBufs(valueColCount);
        for (QVector<double> &buf : valueBufs)
            buf.resize(kChunkRows);
        QVector<FrameColumn> frameCols(valueColCount);
        for (int c = 0; c < valueColCount; ++c)
        {
            frameCols[c].name = headers.at(c + 1);
            frameCols[c].unit = QString();
            frameCols[c].type = ValueType::F64;
            frameCols[c].values = valueBufs.at(c).constData();
            frameCols[c].nulls = nullptr;
        }

        ds = Dataset::create(fi.completeBaseName(), QStringLiteral("csv"));
        int filled = 0;
        qint64 totalRows = 0;
        while (!in.atEnd())
        {
            const QStringList cells = in.readLine().split(QLatin1Char(','));
            if (cells.isEmpty() || cells.first().trimmed().isEmpty())
                continue;

            const double tSec = cells.at(0).trimmed().toDouble();
            timeBuf[filled] = static_cast<TimeUs>(tSec * 1000000.0);
            for (int c = 0; c < valueColCount; ++c)
                valueBufs[c][filled] = (c + 1 < cells.size())
                    ? cells.at(c + 1).trimmed().toDouble()
                    : 0.0;
            ++filled;
            ++totalRows;

            if (filled == kChunkRows)
            {
                QString appendErr;
                if (!ds.appendFrame(tableName, timeBuf.constData(), filled,
                                    frameCols, &appendErr))
                {
                    file.close();
                    return fail(QStringLiteral("etable"),
                                QStringLiteral("灌入数据失败: %1").arg(appendErr));
                }
                filled = 0;
            }
        }
        file.close();

        if (filled > 0)
        {
            QString appendErr;
            if (!ds.appendFrame(tableName, timeBuf.constData(), filled,
                                frameCols, &appendErr))
                return fail(QStringLiteral("etable"),
                            QStringLiteral("灌入数据失败: %1").arg(appendErr));
        }
        if (totalRows <= 0)
            return fail(QStringLiteral("edata"), QStringLiteral("没有数据行"));

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
