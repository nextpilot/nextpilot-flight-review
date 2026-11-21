#pragma once

#include "Dataset.h"
#include "IIntentExecutor.h"
#include "Intent.h" // IIntentHandler 定义在 Intent.h
#include "PythonBridge.h"
#include "Result.h"

#include <QPointer>
#include <QVariantMap>

namespace nextpilot::plotscope::dataset
{
class DatasetRegistry;
}

namespace nextpilot::plotscope::script
{

using nextpilot::plotscope::foundation::Result;
using nextpilot::plotscope::intent::Intent;
using nextpilot::plotscope::intent::IntentResult;

// ============================================================================
// PythonHandler：script.py.* 意图族（M2.6）——Python 子进程桥的意图面。
//
//   script.py.eval     {code}                        -> 值
//   script.py.exec     {source, dataset?, timeout_ms?}
//                      -> {value, out, new, dataset, rows}
//                      dataset 有效时把首表全部数值列经 shm 注入 ds，
//                      脚本 ds['x']=... 的写回列落到临时 .bin（new 字段）
//   script.py.status   {}                            -> {available, interpreter, running, pid}
//   script.py.push     {dataset, column, name?}      -> {rows, name}（单列推给 Python 全局）
//   script.py.pull     {name, want_values?, timeout_ms?}
//                      -> {rows, mean?, values?}（want_values 仅小数组用）
//   script.py.writeback{dataset, columns?}           -> {dataset(v2 id), rows, columns}
//                      末次 exec 的写回列 + 原表全部数值列 = v2 数据集（衔接 M2.5 版本化：
//                      v1 用 "<原名> (py v2)" 命名新快照，原数据集字节不动）
//
// 大数组走 /dev/shm 文件旁路（无 /dev/shm 的平台退到临时目录）：
//   C++ write(8MB) -> Python np.memmap 映射 -> np.asarray().tofile() -> C++ read。
// ============================================================================
class PythonHandler final : public QObject, public nextpilot::plotscope::intent::IIntentHandler
{
    Q_OBJECT
public:
    PythonHandler(nextpilot::plotscope::intent::IIntentExecutor &executor,
                  dataset::DatasetRegistry *data, QObject *parent = nullptr);

    bool canHandle(const Intent &intent) const override
    {
        return intent.op.startsWith(QStringLiteral("script.py."));
    }
    Result<IntentResult> execute(const Intent &intent) override;

    PythonBridge &bridge() { return m_bridge; }

private:
    Result<IntentResult> doExec(const Intent &intent);
    Result<IntentResult> doPush(const Intent &intent);
    Result<IntentResult> doPull(const Intent &intent);
    Result<IntentResult> doWriteback(const Intent &intent);

    // 列读取 + shm 落盘（返回 {列名: {path, rows}} 元数据）。
    Result<QVariantMap> pushColumns(dataset::DatasetId dsId, const QStringList &cols,
                                    QString *tableNameOut);
    static QString makeShmPath();
    static bool writeDoubles(const QString &path, const QVector<double> &v);
    static Result<QVector<double>> readDoubles(const QString &path);

    nextpilot::plotscope::intent::IIntentExecutor &m_executor;
    dataset::DatasetRegistry *m_data = nullptr;
    PythonBridge m_bridge;
    QVariantMap m_lastNew;    // 末次 exec 的写回列元数据 {列名:{path,rows}}
    dataset::DatasetId m_lastDataset = -1; // 写回列的母数据集（writeback 缺省用它）
};

} // namespace nextpilot::plotscope::script
