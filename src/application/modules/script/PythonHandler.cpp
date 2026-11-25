#include "PythonHandler.h"

#include "Dataset.h"
#include "DatasetHub.h"
#include "ErrorCodes.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QStandardPaths>
#include <QProcess>
#include <QUuid>

#include <limits>

using namespace nextpilot::plotscope::foundation;

namespace nextpilot::plotscope::script
{

using nextpilot::plotscope::dataset::Dataset;
using nextpilot::plotscope::dataset::DatasetId;
using nextpilot::plotscope::dataset::DatasetHub;
using nextpilot::plotscope::dataset::kInvalidDataset;
using nextpilot::plotscope::dataset::TimeNs;
using nextpilot::plotscope::dataset::ValueType;
using nextpilot::plotscope::foundation::fail;

namespace
{
int toIntOr(const QVariantMap &params, const QString &key, int def)
{
    return params.contains(key) ? params.value(key).toInt() : def;
}

QString shmDir()
{
    const QDir d(QStringLiteral("/dev/shm"));
    return d.exists() ? d.path()
                      : QStandardPaths::writableLocation(QStandardPaths::TempLocation);
}
} // namespace

PythonHandler::PythonHandler(nextpilot::plotscope::intent::IIntentExecutor &executor,
                             DatasetHub *data, QObject *parent)
    : QObject(parent), m_executor(executor), m_data(data)
{
    setObjectName(QStringLiteral("python-handler"));
}

QString PythonHandler::makeShmPath()
{
    return shmDir() + QStringLiteral("/ngpy-")
           + QUuid::createUuid().toString(QUuid::Id128) + QStringLiteral(".bin");
}

bool PythonHandler::writeDoubles(const QString &path, const QVector<double> &v)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const qint64 want = qint64(v.size()) * qint64(sizeof(double));
    return f.write(reinterpret_cast<const char *>(v.constData()), want) == want;
}

Result<QVector<double>> PythonHandler::readDoubles(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return fail(err::Path, QStringLiteral("shm 文件打不开: %1").arg(path));
    const QByteArray bytes = f.readAll();
    QVector<double> v(bytes.size() / qint64(sizeof(double)));
    memcpy(v.data(), bytes.constData(), v.size() * sizeof(double));
    return Result<QVector<double>>(v);
}

// 把指定列（缺省首表全部数值列）读成 f64 并写 shm，返回协议元数据。
Result<QVariantMap> PythonHandler::pushColumns(DatasetId dsId, const QStringList &cols,
                                               QString *tableNameOut)
{
    if (!m_data)
        return fail(err::State, QStringLiteral("数据面未装配"));
    const Dataset *ds = m_data->get(dsId);
    if (!ds)
        return fail(err::NoEnt, QStringLiteral("数据集 %1 不存在").arg(dsId));
    if (ds->tableCount() == 0)
        return fail(err::Empty, QStringLiteral("数据集没有表"));
    const auto *table = ds->table(0);
    if (table->rows == 0)
        return fail(err::Empty, QStringLiteral("表 %1 是空表").arg(table->name));
    if (tableNameOut)
        *tableNameOut = table->name;

    QStringList want = cols;
    if (want.isEmpty())
    {
        for (const auto &c : table->columns)
            if (c.type != ValueType::Text && c.rowCount > 0)
                want << c.name;
    }
    if (want.isEmpty())
        return fail(err::Empty, QStringLiteral("表里没有可推送的数值列"));

    QVariantMap meta;
    for (const QString &col : want)
    {
        QVector<double> v;
        // M2.7.1：整列一趟 FFI（原逐值循环 100 万行 26ms → memcpy 级 1ms）。
        const int n = ds->readColumnF64(table->name, col, v);
        if (n <= 0)
            return fail(err::Column,
                        QStringLiteral("列 %1.%2 读取失败").arg(table->name, col));
        const QString path = makeShmPath();
        if (!writeDoubles(path, v))
            return fail(err::Path, QStringLiteral("shm 写入失败: %1").arg(path));
        QVariantMap m;
        m.insert(QStringLiteral("path"), path);
        m.insert(QStringLiteral("rows"), n);
        meta.insert(col, m);
    }
    return Result<QVariantMap>(meta);
}

Result<IntentResult> PythonHandler::execute(const Intent &intent)
{
    const QString op = intent.op;
    if (op == QStringLiteral("script.py.eval"))
    {
        const QString code = intent.params.value(QStringLiteral("code")).toString();
        if (code.isEmpty())
            return fail(err::Args, QStringLiteral("script.py.eval 需要 code"));
        const int timeout = toIntOr(intent.params, QStringLiteral("timeout_ms"), 30000);
        const Result<QVariant> r = m_bridge.eval(code, timeout);
        if (!r)
            return fail(r.error().code, r.error().message);
        return Result<IntentResult>(IntentResult{r.value()});
    }
    if (op == QStringLiteral("script.py.status"))
    {
        QVariantMap st;
        const bool available = !QStandardPaths::findExecutable(m_bridge.interpreter()).isEmpty();
        st.insert(QStringLiteral("available"), available);
        st.insert(QStringLiteral("interpreter"), m_bridge.interpreter());
        // numpy 是否可用（桥的大数组旁路硬依赖它）：一次探测 + 缓存。
        // 没有 numpy 时 eval/exec 全部会失败，UI 与测试要能提前知道原因，而不是
        // 只看到一句「Python 桥子进程退出」。
        if (available && !m_numpyChecked)
        {
            QProcess probe;
            probe.start(m_bridge.interpreter(),
                        {QStringLiteral("-c"), QStringLiteral("import numpy")});
            m_numpyChecked = true;
            m_numpyAvailable = probe.waitForFinished(10000)
                               && probe.exitStatus() == QProcess::NormalExit
                               && probe.exitCode() == 0;
            m_numpyError = m_numpyAvailable
                ? QString()
                : QString::fromUtf8(probe.readAllStandardError()).trimmed().right(200);
        }
        st.insert(QStringLiteral("numpy"), m_numpyAvailable);
        if (!m_numpyAvailable && !m_numpyError.isEmpty())
            st.insert(QStringLiteral("numpy_error"), m_numpyError);
        st.insert(QStringLiteral("running"), m_bridge.running());
        st.insert(QStringLiteral("error_tail"), m_bridge.lastStderr().trimmed().right(300));
        return Result<IntentResult>(IntentResult{st});
    }
    if (op == QStringLiteral("script.py.exec"))
        return doExec(intent);
    if (op == QStringLiteral("script.py.push"))
        return doPush(intent);
    if (op == QStringLiteral("script.py.pull"))
        return doPull(intent);
    if (op == QStringLiteral("script.py.writeback"))
        return doWriteback(intent);
    return fail(err::Unsupported, QStringLiteral("未知 op: %1").arg(op));
}

Result<IntentResult> PythonHandler::doExec(const Intent &intent)
{
    const QString source = intent.params.value(QStringLiteral("source")).toString();
    if (source.isEmpty())
        return fail(err::Args, QStringLiteral("script.py.exec 需要 source"));
    const QVariant dsParam = intent.params.value(QStringLiteral("dataset"));
    bool dsOk = false;
    const DatasetId dsParsed = dsParam.toInt(&dsOk);
    const DatasetId dsId = dsOk ? dsParsed : kInvalidDataset;

    QVariantMap cols;
    int rows = 0;
    if (dsId != kInvalidDataset)
    {
        QString tableName;
        const Result<QVariantMap> pm = pushColumns(dsId, {}, &tableName);
        if (!pm)
            return fail(pm.error().code, pm.error().message);
        cols = pm.value();
        rows = cols.isEmpty()
                   ? 0
                   : cols.constBegin().value().toMap().value(QStringLiteral("rows")).toInt();
    }

    const QDir outDir(shmDir());
    const QString outPath =
        outDir.filePath(QStringLiteral("ngpy-out-%1")
                            .arg(QUuid::createUuid().toString(QUuid::Id128)));
    if (!QDir().mkpath(outPath))
        return fail(err::Path, QStringLiteral("写回目录创建失败: %1").arg(outPath));

    const int timeout = toIntOr(intent.params, QStringLiteral("timeout_ms"), 60000);
    const Result<QVariantMap> r = m_bridge.exec(source, cols, outPath, timeout);
    // 输入列的 shm 文件已消费：无论成败立即清理（写回列由 writeback 消费）
    for (auto it = cols.constBegin(); it != cols.constEnd(); ++it)
        QFile::remove(it.value().toMap().value(QStringLiteral("path")).toString());

    if (!r)
        return fail(r.error().code, r.error().message);
    const QVariantMap resp = r.value();

    m_lastNew = resp.value(QStringLiteral("new")).toMap();
    m_lastDataset = dsId;
    QVariantMap payload;
    payload.insert(QStringLiteral("value"), resp.value(QStringLiteral("value")));
    payload.insert(QStringLiteral("out"), resp.value(QStringLiteral("out")).toString());
    payload.insert(QStringLiteral("new"), m_lastNew);
    payload.insert(QStringLiteral("dataset"), dsId);
    payload.insert(QStringLiteral("rows"), rows);
    return Result<IntentResult>(IntentResult{payload});
}

Result<IntentResult> PythonHandler::doPush(const Intent &intent)
{
    const QVariant dsParam = intent.params.value(QStringLiteral("dataset"));
    bool dsOk = false;
    const DatasetId dsParsed = dsParam.toInt(&dsOk);
    const DatasetId dsId = dsOk ? dsParsed : kInvalidDataset;
    if (dsId == kInvalidDataset)
        return fail(err::Args, QStringLiteral("script.py.push 需要 dataset"));
    const QString column = intent.params.value(QStringLiteral("column")).toString();
    if (column.isEmpty())
        return fail(err::Args, QStringLiteral("script.py.push 需要 column"));
    const QString name =
        intent.params.value(QStringLiteral("name")).toString().isEmpty()
            ? column
            : intent.params.value(QStringLiteral("name")).toString();

    QString tableName;
    const Result<QVariantMap> pm = pushColumns(dsId, {column}, &tableName);
    if (!pm)
        return fail(pm.error().code, pm.error().message);
    const QVariantMap cm = pm.value().value(column).toMap();
    const int rows = cm.value(QStringLiteral("rows")).toInt();
    const QString path = cm.value(QStringLiteral("path")).toString();

    const Result<QVariantMap> r = m_bridge.put(name, path, rows);
    QFile::remove(path); // put 是 memmap 映射，文件暂不能删？
    // 注意：np.memmap 映射期间删除文件在 Linux 上是安全的（inode 保持到映射关闭），
    // Windows 上会失败——可接受（临时目录兜底）。
    if (!r)
        return fail(r.error().code, r.error().message);
    QVariantMap payload;
    payload.insert(QStringLiteral("rows"), rows);
    payload.insert(QStringLiteral("name"), name);
    payload.insert(QStringLiteral("table"), tableName);
    return Result<IntentResult>(IntentResult{payload});
}

Result<IntentResult> PythonHandler::doPull(const Intent &intent)
{
    const QString name = intent.params.value(QStringLiteral("name")).toString();
    if (name.isEmpty())
        return fail(err::Args, QStringLiteral("script.py.pull 需要 name"));
    const bool wantValues =
        intent.params.value(QStringLiteral("want_values")).toBool();
    const int timeout = toIntOr(intent.params, QStringLiteral("timeout_ms"), 30000);

    const QString path = makeShmPath();
    const Result<QVariantMap> r = m_bridge.pull(name, path, timeout);
    if (!r)
    {
        QFile::remove(path);
        return fail(r.error().code, r.error().message);
    }
    const int rows = r.value().value(QStringLiteral("rows")).toInt();

    QVariantMap payload;
    payload.insert(QStringLiteral("rows"), rows);
    payload.insert(QStringLiteral("name"), name);
    const Result<QVector<double>> vals = readDoubles(path);
    QFile::remove(path);
    if (!vals)
        return fail(vals.error().code, vals.error().message);
    double sum = 0;
    for (double x : vals.value())
        sum += x;
    payload.insert(QStringLiteral("mean"),
                   rows ? sum / rows : 0.0);
    if (wantValues)
    {
        QVariantList lv;
        lv.reserve(vals.value().size());
        for (double x : vals.value())
            lv.append(x);
        payload.insert(QStringLiteral("values"), lv);
    }
    return Result<IntentResult>(IntentResult{payload});
}

Result<IntentResult> PythonHandler::doWriteback(const Intent &intent)
{
    if (!m_data)
        return fail(err::State, QStringLiteral("数据面未装配"));
    bool dsOk = false;
    const DatasetId dsParsed = intent.params.value(QStringLiteral("dataset")).toInt(&dsOk);
    DatasetId dsId = dsOk ? dsParsed : kInvalidDataset;
    if (dsId == kInvalidDataset)
        dsId = m_lastDataset;
    if (dsId == kInvalidDataset)
        return fail(err::Args,
                    QStringLiteral("writeback 需要 dataset（或此前 exec 已指定）"));
    const Dataset *orig = m_data->get(dsId);
    if (!orig)
        return fail(err::NoEnt, QStringLiteral("数据集 %1 不存在").arg(dsId));
    if (m_lastNew.isEmpty())
        return fail(err::Empty,
                    QStringLiteral("没有可写回的列（先在 exec 里给 ds['新列'] 赋值）"));

    QStringList cols;
    const QVariant colsParam = intent.params.value(QStringLiteral("columns"));
    if (colsParam.typeId() == QMetaType::QStringList || colsParam.canConvert<QStringList>())
        cols = colsParam.toStringList();
    if (!cols.isEmpty())
    {
        for (const QString &c : cols)
            if (!m_lastNew.contains(c))
                return fail(err::Column,
                            QStringLiteral("写回列 %1 不存在（可用: %2）")
                                .arg(c, m_lastNew.keys().join(QStringLiteral(", "))));
    }

    // 组装 v2（M2.7.5 切片：全时间范围 + 非文本列子集，物化一趟 ABI 完成；
    // 原数据集不动——不可变快照，切片集独立生命周期）。列类型原样保留。
    const auto *table = orig->table(0);
    QVector<QString> keepCols;
    keepCols.reserve(table->columns.size());
    for (const auto &c : table->columns)
        if (c.type != ValueType::Text)
            keepCols << c.name;
    // 时间范围用极值：切片按整型微秒取行区间，避免经 f64 秒往返的舍入漂移。
    constexpr TimeNs kAllTime = std::numeric_limits<TimeNs>::min();
    constexpr TimeNs kEnd = std::numeric_limits<TimeNs>::max();
    QString sliceErr;
    auto sliced = orig->slice(table->name, keepCols, kAllTime, kEnd, &sliceErr);
    if (!sliced)
        return fail(err::Data, QStringLiteral("v2 切片失败: %1").arg(sliceErr));
    Dataset v2 = std::move(*sliced);
    v2.setName(orig->name() + QStringLiteral(" (py v2)"));
    for (auto it = m_lastNew.constBegin(); it != m_lastNew.constEnd(); ++it)
    {
        if (!cols.isEmpty() && !cols.contains(it.key()))
            continue;
        const QVariantMap m = it.value().toMap();
        const Result<QVector<double>> vals = readDoubles(
            m.value(QStringLiteral("path")).toString());
        if (!vals)
            return fail(vals.error().code, vals.error().message);
        if (vals.value().size() != table->rows)
            return fail(err::Data,
                        QStringLiteral("写回列 %1 行数 %2 != %3")
                            .arg(it.key())
                            .arg(vals.value().size())
                            .arg(table->rows));
        v2.addF64Column(table->name, it.key(), QString(), vals.value());
    }
    v2.refreshSchema();

    const Result<DatasetId> added = m_data->addWithId(std::move(v2), orig->name()
                                                                        + QStringLiteral(" (py v2)"));
    if (!added)
        return fail(added.error().code, added.error().message);

    // 写回列 .bin 已消费，清理
    const QStringList writtenNames = cols.isEmpty() ? m_lastNew.keys() : cols;
    QVariantList written;
    for (const QString &s : writtenNames)
        written << s;
    for (auto it = m_lastNew.constBegin(); it != m_lastNew.constEnd(); ++it)
        QFile::remove(it.value().toMap().value(QStringLiteral("path")).toString());
    m_lastNew.clear();

    QVariantMap payload;
    payload.insert(QStringLiteral("dataset"), added.value());
    payload.insert(QStringLiteral("rows"), table->rows);
    payload.insert(QStringLiteral("columns"), written);
    return Result<IntentResult>(IntentResult{payload});
}

} // namespace nextpilot::plotscope::script
