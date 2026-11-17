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
        const QStringList columns = headerLine.split(QLatin1Char(','));
        if (columns.isEmpty())
            return fail(QStringLiteral("eformat"), QStringLiteral("无表头"));

        Dataset ds(QFileInfo(path).baseName());
        ds.setFormat(QStringLiteral("csv"));

        // 第一列当时间轴，其余列各成一个 Series。
        QVector<QVector<double>> cols(columns.size());
        while (!in.atEnd())
        {
            const QStringList cells = in.readLine().split(QLatin1Char(','));
            for (int i = 0; i < columns.size() && i < cells.size(); ++i)
                cols[i].push_back(cells.at(i).trimmed().toDouble());
        }
        if (cols.isEmpty() || cols.first().isEmpty())
            return fail(QStringLiteral("edata"), QStringLiteral("没有数据行"));

        const QVector<double> &t = cols.first();
        for (int c = 1; c < columns.size(); ++c)
        {
            SeriesData s;
            s.name = columns.at(c).trimmed();
            s.time = t;
            s.value = cols.at(c);
            ds.addSeries(std::move(s));
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
