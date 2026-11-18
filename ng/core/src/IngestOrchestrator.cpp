#include "IngestOrchestrator.h"

#include "DataSourceRepository.h"

#include <QFileInfo>

namespace ng::core
{

IngestOrchestrator::IngestOrchestrator(DataRepository &repo, DataSourceRepository &sources,
                                       IngestPipeline &pipeline)
    : m_repo(repo), m_sources(sources), m_pipeline(pipeline)
{
}

Result<int> IngestOrchestrator::openFile(const QString &path, const QStringList &parserIds,
                                         const QString &topicFilter)
{
    if (path.isEmpty())
        return fail(QStringLiteral("eio"), QStringLiteral("路径为空"));

    FileByteSource source(path);
    if (!source.valid())
        return fail(QStringLiteral("eio"), source.error());

    return ingest(source, parserIds, topicFilter, path);
}

Result<int> IngestOrchestrator::openSource(IByteSource &source, const QStringList &parserIds,
                                            const QString &topicFilter, const QString &origin)
{
    return ingest(source, parserIds, topicFilter, origin);
}

Result<int> IngestOrchestrator::ingest(IByteSource &source, const QStringList &parserIds,
                                        const QString &topicFilter, const QString &origin)
{
    // --- 1) 定解析器：显式指定 > 按后缀 > 按内容嗅探 ---
    QStringList ids = parserIds;
    if (ids.isEmpty())
    {
        // 按后缀找（".ulg" / "ulg" 都认）
        const QString suffix = QFileInfo(origin).suffix().toLower();
        if (!suffix.isEmpty())
        {
            if (const ParserEntry *e = ParserRegistry::findByExtension(QStringLiteral(".") + suffix))
                ids << e->info.id;
        }
        // 后缀找不到就嗅探内容（读一小段头）
        if (ids.isEmpty())
        {
            char head[64];
            const qint64 n = source.read(head, sizeof(head));
            if (n > 0)
            {
                if (const ParserEntry *e = ParserRegistry::findBySniff(QByteArray(head, int(n))))
                    ids << e->info.id;
            }
        }
    }
    if (ids.isEmpty())
    {
        return fail(QStringLiteral("eformat"),
                    QStringLiteral("没有匹配的解析器: %1").arg(origin));
    }

    // --- 2) 建数据源（运行时实例）---
    DataSourceInfo info;
    info.name = origin.isEmpty() ? QStringLiteral("source") : origin;
    info.origin = origin;
    info.kind = SourceKind::File;
    info.state = SourceState::Opening;

    // 解析绑定：扇出的各路（同一份字节喂给所有解析器）。
    for (const QString &pid : ids)
    {
        ParserBinding b;
        b.parserId = pid;
        b.topic = topicFilter;
        b.active = true;
        info.parsers << b;
    }

    const int sourceId = m_sources.add(info);
    if (sourceId < 0)
        return fail(QStringLiteral("eio"), QStringLiteral("数据源登记失败"));
    m_sourceId = sourceId;

    // --- 3) 绑定解析器：每个 (源, 解析器) 一个独立实例 + 独立数据集 ---
    m_datasetIds.clear();
    for (const QString &pid : ids)
    {
        ParserBinding b;
        b.parserId = pid;
        b.topic = topicFilter;
        b.active = true;

        const Result<int> bound = m_pipeline.bindParser(sourceId, b);
        if (!bound)
        {
            m_sources.remove(sourceId);
            return fail(bound.error().code,
                        QStringLiteral("绑定解析器 %1 失败: %2").arg(pid, bound.error().message));
        }
        m_datasetIds << bound.value();
        m_sources.addDataset(sourceId, bound.value());
    }

    // --- 4) 读字节 -> 喂给所有活跃通道（扇出）---
    m_sources.setState(sourceId, SourceState::Streaming);
    QByteArray buffer;
    qint64 totalRead = 0;
    while (true)
    {
        const qint64 n = source.readChunk(buffer, 256 * 1024);
        if (n < 0)
        {
            m_sources.setState(sourceId, SourceState::Error,
                                      QStringLiteral("读取失败"));
            m_sources.remove(sourceId);
            return fail(QStringLiteral("eio"), QStringLiteral("读取失败: %1").arg(origin));
        }
        if (n == 0)
            break;

        totalRead += n;
        m_pipeline.feed(sourceId, reinterpret_cast<const uint8_t *>(buffer.constData()), n);

        if (m_sources.setState(sourceId, SourceState::Streaming))
        {
            // 进度不回写（高频路径不做额外工作），但保留 bytesRead 语义。
        }
        // 源已到末尾（文件）：再读一次会返回 0，循环自然结束。
        if (source.atEnd())
        {
            // 继续读一次确认没有残留（某些实现最后一读会返回 0）。
            const qint64 extra = source.readChunk(buffer, 256 * 1024);
            if (extra <= 0)
                break;
            totalRead += extra;
            m_pipeline.feed(sourceId, reinterpret_cast<const uint8_t *>(buffer.constData()),
                            extra);
        }
    }

    // --- 5) 收尾：冲刷残留（尾半帧、索引表、参数段）+ 刷新 schema ---
    m_pipeline.finish(sourceId);

    // --- 6) 登记状态 ---
    m_sources.setState(sourceId, SourceState::Closed);
    if (m_datasetIds.isEmpty())
    {
        m_sources.remove(sourceId);
        return fail(QStringLiteral("edata"), QStringLiteral("没有解析出任何数据: %1").arg(origin));
    }
    return m_datasetIds.first();
}

} // namespace ng::core
