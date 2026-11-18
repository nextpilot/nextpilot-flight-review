#include "CsvExtension.h"

#include "DataRepository.h"
#include "Dataset.h"
#include "Intent.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>

using namespace ng::core;
using namespace ng::host;

// 意图处理器：识别 "data.load" 且 format=csv 的意图，读取文件产出 Dataset。
// DataSet 本体由 Rust kernel 持有：一张 CSV = 一张 typed 表，首列共享时间轴（UTC 微秒），
// 其余列各成 F64 typed 列。加载前先查 ps-cache（按 size/mtime 指纹），命中则不重解析。
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

        // 2) 未命中：解析 CSV → 用 C ABI 构造 Rust typed 表。
        if (!fromCache)
        {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                return fail(QStringLiteral("eio"), QStringLiteral("无法打开: %1").arg(path));
            QTextStream in(&file);
            if (in.atEnd())
                return fail(QStringLiteral("eempty"), QStringLiteral("文件为空"));

            const QString headerLine = in.readLine();
            QStringList columns = headerLine.split(QLatin1Char(','));
            for (QString &c : columns)
                c = c.trimmed();
            if (columns.isEmpty())
                return fail(QStringLiteral("eformat"), QStringLiteral("无表头"));

            QVector<QVector<double>> cols(columns.size());
            while (!in.atEnd())
            {
                const QStringList cells = in.readLine().split(QLatin1Char(','));
                for (int i = 0; i < columns.size() && i < cells.size(); ++i)
                    cols[i].push_back(cells.at(i).trimmed().toDouble());
            }
            file.close();
            if (cols.isEmpty() || cols.first().isEmpty())
                return fail(QStringLiteral("edata"), QStringLiteral("没有数据行"));

            QVector<TimeUs> timeUs;
            timeUs.reserve(cols.first().size());
            for (double t : cols.first())
                timeUs.push_back(static_cast<TimeUs>(t * 1000000.0));

            ds = Dataset::create(fi.completeBaseName(), QStringLiteral("csv"));
            if (ds.addTable(tableName, timeUs) < 0)
                return fail(QStringLiteral("etable"), QStringLiteral("建表失败（重名/时间轴为空）"));
            for (int c = 1; c < columns.size(); ++c)
            {
                QVector<bool> nulls; // CSV 空值场景可扩展；当前全部有效
                if (!ds.addF64Column(tableName, columns.at(c), QString(), cols.at(c), &nulls))
                    return fail(QStringLiteral("ecolumn"), QStringLiteral("添加列失败: %1").arg(columns.at(c)));
            }

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
