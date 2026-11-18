#include "CsvExtension.h"

#include "DataRepository.h"
#include "Dataset.h"
#include "Intent.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <algorithm>

using namespace ng::core;
using namespace ng::host;

namespace
{
// 流式分块大小：C++ 侧缓冲恒定，与文件大小无关。
// 一批 4096 行 × 列数 × 8B；即便百万行也只有这一批驻留。
constexpr int kChunkRows = 4096;
} // namespace

// 意图处理器：识别 "data.load" 且 format=csv 的意图，读取文件产出 Dataset。
//
// 内存铁律（面向百万级数据）：
//   - 绝不把整文件读进 QVector<QVector<double>>；逐行解析进固定大小的分块缓冲，
//     每满一批通过 Dataset::appendFrame 灌入 Rust，缓冲复用。C++ 峰值 = 一批。
//   - 数据本体、时间轴、缓存全在 Rust；C++ 只持 PsDataset 句柄 + O(列数) 的 schema。
//   - 加载前先查 ps-cache（按 size/mtime 指纹），命中则连解析都跳过。
class CsvExtension::Handler final : public QObject, public IIntentHandler
{
    Q_OBJECT
public:
    explicit Handler(DataRepository &repo, QObject *parent)
        : QObject(parent), m_repo(repo) {}

    bool canHandle(const QString &op) const override
    {
        return op == QStringLiteral("data.load");
    }

    Result<IntentResult> execute(const Intent &intent) override
    {
        const QString path = intent.params.value(QStringLiteral("path")).toString();
        const QString fmt = intent.params.value(QStringLiteral("format"), QStringLiteral("csv")).toString();
        if (fmt != QStringLiteral("csv"))
            return fail(QStringLiteral("eformat"), QStringLiteral("非 CSV 格式: %1").arg(fmt));

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

        // 1) 缓存优先：命中则直接还原 Rust 数据集，跳过解析。
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

        // 2) 未命中：流式解析 CSV → appendFrame 灌入 Rust typed 表。
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

            // 第一列为共享时间轴；其余为 typed 列（当前统一 F64）。
            const int valueColCount = headers.size() - 1;
            if (valueColCount <= 0)
                return fail(QStringLiteral("eformat"), QStringLiteral("无数据列"));

            // 固定分块缓冲：一次只驻留 kChunkRows 行，循环复用。
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
                frameCols[c].nulls = nullptr; // CSV 空值场景可扩展；当前全部有效
            }

            ds = Dataset::create(fi.completeBaseName(), QStringLiteral("csv"));
            int filled = 0; // 当前批已填行数
            qint64 totalRows = 0;
            while (!in.atEnd())
            {
                const QStringList cells = in.readLine().split(QLatin1Char(','));
                if (cells.isEmpty() || cells.first().trimmed().isEmpty())
                    continue; // 跳过空行

                // 时间：第一列按秒解析 → UTC 微秒。
                const double tSec = cells.at(0).trimmed().toDouble();
                timeBuf[filled] = static_cast<TimeUs>(tSec * 1000000.0);
                for (int c = 0; c < valueColCount; ++c)
                    valueBufs[c][filled] = (c + 1 < cells.size())
                        ? cells.at(c + 1).trimmed().toDouble()
                        : 0.0;
                ++filled;
                ++totalRows;

                // 满一批 → 立即灌入 Rust，缓冲复用。
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

            // 尾批（不足一批）。
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

            // 所有批次灌完后刷新一次 schema（O(列数 + 时间轴转换一次)）。
            ds.refreshSchema();

            // 3) 写缓存（原子写，按源文件指纹）；缓存失败不影响加载成功。
            QString cacheErr;
            ds.saveCache(cachePath, sourceSize, sourceMtime, &cacheErr);
        }

        // 意图可携带稳定 id（录制/回放用）；未指定则用仓库自增 id。
        DatasetId id;
        if (intent.params.contains(QStringLiteral("id")))
        {
            const DatasetId wanted = intent.params.value(QStringLiteral("id")).toInt();
            Result<DatasetId> added = m_repo.addWithStableId(std::move(ds), wanted);
            if (!added)
                return fail(added.error().code, added.error().message);
            id = added.value();
        }
        else
        {
            id = m_repo.add(std::move(ds));
        }
        IntentResult r;
        r.payload = id;
        return r;
    }

private:
    DataRepository &m_repo;
};

ModuleInfo CsvExtension::info() const
{
    return ModuleInfo{
        QStringLiteral("ext.csv"),
        QStringLiteral("CSV 格式扩展"),
        QStringLiteral("1.0"),
        ModuleKind::Extension,
        {QStringLiteral("format.csv")},
        {}
    };
}

void CsvExtension::initialize(ModuleContext &ctx)
{
    m_handler = new Handler(ctx.data(), this); // parent=this，所有权即撤销
    ctx.registerIntentHandler(m_handler);
}

#include "CsvExtension.moc"
