#include "DataLoadHandler.h"

#include "DataRepository.h"
#include "IngestOrchestrator.h"
#include "IngestPipeline.h"

using namespace ng::core;

bool DataLoadHandler::canHandle(const Intent &intent) const
{
    return intent.op == QStringLiteral("data.load");
}

Result<IntentResult> DataLoadHandler::execute(const Intent &intent)
{
    const QString path = intent.params.value(QStringLiteral("path")).toString();
    const QString format = intent.params.value(QStringLiteral("format")).toString();

    IFormatDriver *driver = format.isEmpty()
        ? m_registry.driverForPath(path)
        : m_registry.driverForFormat(format);

    // format 显式但按后缀能兜底时再试一次（容忍 format 拼写与后缀不一致）。
    if (!driver && !format.isEmpty())
        driver = m_registry.driverForPath(path);

    if (!driver)
        return fail(QStringLiteral("eformat"),
                    QStringLiteral("没有已注册的格式驱动能处理: %1 (format=%2)")
                        .arg(path, format));

    // --- 走统一摄取管道（归并后的主路径）---
    //
    // 驱动只**声明**"这个格式由哪个解析器负责"（parserId()），
    // 读字节与扇出都由宿主编排——于是每种格式只有一份解析器实现，
    // 且录制旁路只需一个 tee 就能覆盖所有格式。
    const QString parserId = driver->parserId();
    if (m_pipeline && !parserId.isEmpty())
    {
        // 宿主策略（三项都不是解析器的事）：
        //   · 表名：解析器只认字节，给不出"这张表叫什么" —— 由驱动声明；
        //   · 缓存：按源文件指纹复用已解析的数据集；
        //   · 稳定 id：录制/回放时数据集 id 不能漂移。
        IngestOrchestrator::Options options;

        // 表名：驱动的 preferredTableName 给出目标表名（单表格式才声明）。
        // ULog 这类多表格式返回空串 —— 表名由 topic 决定，宿主不插手。
        options.tableName = driver->preferredTableName(intent, path);

        // 缓存：默认开启（与旧 driver 行为一致），nocache=true 时关闭。
        options.useCache = !intent.params.value(QStringLiteral("nocache"), false).toBool();

        // 稳定 id：脚本可显式指定，录制/回放据此保持 id 一致。
        if (intent.params.contains(QStringLiteral("id")))
            options.stableId = intent.params.value(QStringLiteral("id")).toInt();

        IngestOrchestrator orchestrator(m_repo, m_sources, *m_pipeline);
        const Result<int> opened = orchestrator.openFile(path, QStringList{parserId},
                                                        QString(), options);
        if (!opened)
            return fail(opened.error().code, opened.error().message);

        IntentResult r;
        r.payload = opened.value();
        return r;
    }

    // --- 旧路径：驱动自己读文件自己建 Dataset（尚未归并的格式）---
    return driver->load(intent, m_repo);
}
