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
// 数据模型对齐 kernel：一张 CSV = 一张表，第一列为共享时间轴（UTC 微秒），
// 其余列各成一张 typed 列，表内所有列共享这条时间轴。
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

        // 逐行读取，第一列当时间（秒），其余列当数值。
        QVector<QVector<double>> cols(columns.size());
        while (!in.atEnd())
        {
            const QStringList cells = in.readLine().split(QLatin1Char(','));
            for (int i = 0; i < columns.size() && i < cells.size(); ++i)
                cols[i].push_back(cells.at(i).trimmed().toDouble());
        }
        if (cols.isEmpty() || cols.first().isEmpty())
            return fail(QStringLiteral("edata"), QStringLiteral("没有数据行"));

        // 共享时间轴：由第一列换算成 UTC 微秒（kernel 表级时间轴）。
        QVector<TimeUs> timeUs;
        timeUs.reserve(cols.first().size());
        for (double t : cols.first())
            timeUs.push_back(static_cast<TimeUs>(t * 1000000.0));

        // 一张 CSV = 一张表，表名取文件名（对齐"一个 topic 一张表"）。
        const QString tableName = intent.params.value(QStringLiteral("table"),
                                                      QFileInfo(path).completeBaseName()).toString();
        Dataset ds(QFileInfo(path).baseName());
        ds.setFormat(QStringLiteral("csv"));
        const int tableIdx = ds.addTable(tableName, timeUs);
        if (tableIdx < 0)
            return fail(QStringLiteral("etable"), QStringLiteral("建表失败（重名/时间轴非单调/为空）"));

        // 其余列各成一张 typed 列，共享表级时间轴；跳过时间列本身。
        for (int c = 1; c < columns.size(); ++c)
        {
            Column col;
            col.name = columns.at(c);
            col.type = ValueType::F64;
            col.values.reserve(cols.at(c).size());
            for (double v : cols.at(c))
                col.values.push_back(v);
            if (!ds.addColumn(tableName, std::move(col)))
                return fail(QStringLiteral("ecolumn"), QStringLiteral("添加列失败: %1").arg(columns.at(c)));
        }

        const DatasetId id = m_repo.add(std::move(ds));
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
