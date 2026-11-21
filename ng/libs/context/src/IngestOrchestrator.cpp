#include "IngestOrchestrator.h"
#include "ErrorCodes.h"

#include "SourceRegistry.h"

#include <QFileInfo>

namespace nextpilot::plotscope::context
{
namespace err = nextpilot::plotscope::foundation::err;
using nextpilot::plotscope::dataset::Dataset;
using nextpilot::plotscope::dataset::SourceKind;
using nextpilot::plotscope::dataset::SourceState;

using nextpilot::plotscope::parser::ParserEntry;
using nextpilot::plotscope::parser::IByteSource;
using nextpilot::plotscope::parser::FileByteSource;


namespace
{
// 缓存路径：<源文件绝对路径>.<解析器id>.psc
//
// 为什么带解析器 id：管道路径下**每个解析器一个数据集**（扇出的各路），
// 缓存的正是"某一路解析出来的数据集"，所以粒度必须是 (源, 解析器)。
// 单路时就是 "<源文件>.psc" —— 与旧 driver 路径的缓存文件名一致。
//
// 为什么取绝对路径：源路径可能是相对路径（命令行给的），
// 缓存若也用相对路径，就会落到**当前工作目录**而不是源文件旁边 ——
// 换个目录跑同一条命令就找不到缓存，还会在 CWD 留垃圾。
QString cachePathFor(const QString &origin, const QString &parserId)
{
    const QFileInfo fi(origin);
    const QString abs = fi.absoluteFilePath();
    return QStringLiteral("%1.%2.psc").arg(abs, parserId);
}
} // namespace

IngestOrchestrator::IngestOrchestrator(DatasetRegistry &repo, SourceRegistry &sources,
                                       IngestPipeline &pipeline)
    : m_repo(repo), m_sources(sources), m_pipeline(pipeline)
{
}

Result<int> IngestOrchestrator::openFile(const QString &path, const QStringList &parserIds,
                                         const QString &topicFilter, const Options &options)
{
    if (path.isEmpty())
        return fail(err::Io, QStringLiteral("路径为空"));

    FileByteSource source(path);
    if (!source.valid())
        return fail(err::Io, source.error());

    return ingest(source, parserIds, topicFilter, path, options);
}

Result<int> IngestOrchestrator::openSource(IByteSource &source, const QStringList &parserIds,
                                            const QString &topicFilter, const QString &origin,
                                            const Options &options)
{
    return ingest(source, parserIds, topicFilter, origin, options);
}

Result<int> IngestOrchestrator::ingest(IByteSource &source, const QStringList &parserIds,
                                        const QString &topicFilter, const QString &origin,
                                        const Options &options)
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
        return fail(err::Format,
                    QStringLiteral("没有匹配的解析器: %1").arg(origin));
    }

    // --- 2) 建数据源（运行时实例）---
    DataSourceInfo info;
    // name 是**显示名**（文件名），origin 是完整路径——两者分开存，
    // 视图直接显示 name，不必自己猜"这是路径还是名字"（早前把完整路径塞进
    // name，导致列表里显示一长串路径还被列宽截断）。
    info.name = origin.isEmpty() ? QStringLiteral("source") : QFileInfo(origin).fileName();
    if (info.name.isEmpty())
        info.name = QStringLiteral("source");
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
        // 解析参数透传：宿主不解释内容，由各解析器实例的 configure 消费。
        b.options = options.parserOptions;
        info.parsers << b;
    }

    const int sourceId = m_sources.add(info);
    if (sourceId < 0)
        return fail(err::Io, QStringLiteral("数据源登记失败"));
    m_sourceId = sourceId;

    // --- 3) 绑定解析器：每个 (源, 解析器) 一个独立实例 + 独立数据集 ---
    //
    // 缓存命中的一路**不绑定解析器**：那份字节不必再解析，直接还原数据集。
    // 于是"缓存"省掉的是解析开销，不是入仓开销。
    m_datasetIds.clear();
    QVector<QString> toParse;   // 需要真正解析的解析器
    for (const QString &pid : ids)
    {
        // 缓存路径按 (源文件, 解析器) 粒度：每个通道一个数据集，各存各的。
        // 只对**文件**来源有意义（流没有稳定指纹）。
        const QString cachePath = options.useCache ? cachePathFor(origin, pid) : QString();
        bool fromCache = false;
        if (!cachePath.isEmpty())
        {
            const QFileInfo fi(origin);
            QString cacheErr;
            if (auto cached = Dataset::loadCache(cachePath, fi.size(),
                                                 fi.lastModified().toMSecsSinceEpoch(), &cacheErr))
            {
                // 还原的数据集要与"真解析出来的那份"同名同格式，
                // 否则同一份数据两次打开显示不同名字。
                cached->setName(QStringLiteral("src%1_%2").arg(sourceId).arg(pid));
                cached->setFormat(pid);
                const DatasetId dsId =
                    (ids.size() == 1 && options.stableId != kInvalidDataset)
                        ? [&] {
                              const Result<DatasetId> a =
                                  m_repo.addWithStableId(std::move(*cached), options.stableId);
                              return a ? a.value() : m_repo.add(std::move(*cached));
                          }()
                        : m_repo.add(std::move(*cached));
                m_datasetIds << dsId;
                m_sources.addDataset(sourceId, dsId);
                fromCache = true;
            }
        }
        if (fromCache)
            continue;

        toParse << pid;

        ParserBinding b;
        b.parserId = pid;
        b.topic = topicFilter;
        b.active = true;
        // 解析参数透传：宿主不解释内容，由各解析器实例的 configure 消费。
        b.options = options.parserOptions;

        // stable id 只对**单路**有意义（一 id 对多数据集语义不成立）。
        // 多路扇出时仍按自增分配。
        const DatasetId preferred = (ids.size() == 1) ? options.stableId : kInvalidDataset;
        const Result<int> bound = m_pipeline.bindParser(sourceId, b, preferred);
        if (!bound)
        {
            m_sources.remove(sourceId);
            return fail(bound.error().code,
                        QStringLiteral("绑定解析器 %1 失败: %2").arg(pid, bound.error().message));
        }
        m_datasetIds << bound.value();
        m_sources.addDataset(sourceId, bound.value());

        // 表名改写（宿主策略）：解析器只认字节，给不出"这张表叫什么"。
        // 单表格式由宿主命名，这里声明，管道落库时套用。
        if (!options.tableName.isEmpty())
            m_pipeline.setTableRename(sourceId, pid, options.tableName);
    }

    // 全部命中缓存：不必读字节。
    if (toParse.isEmpty())
    {
        m_sources.setState(sourceId, SourceState::Closed);
        // 字节态回写：缓存命中视作整份已读（数据源信息要显示"已读/总量"，
        // 早前 bytesRead 从不赋值，对话框里永远是 0）。
        if (const DataSourceInfo *cur = m_sources.get(sourceId))
        {
            const QFileInfo fi(origin);
            DataSourceInfo copy = *cur;
            copy.bytesRead = fi.size();
            copy.bytesTotal = fi.size();
            m_sources.update(sourceId, copy);
        }
        if (m_datasetIds.isEmpty())
        {
            m_sources.remove(sourceId);
            return fail(err::Data,
                        QStringLiteral("没有解析出任何数据: %1").arg(origin));
        }
        return m_datasetIds.first();
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
            return fail(err::Io, QStringLiteral("读取失败: %1").arg(origin));
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
    // 字节态回写：文件源读完一次落定，成本可忽略；流式的高频路径仍然不回写。
    if (const DataSourceInfo *cur = m_sources.get(sourceId))
    {
        const QFileInfo fi(origin);
        DataSourceInfo copy = *cur;
        copy.bytesRead = totalRead;
        copy.bytesTotal = fi.exists() ? fi.size() : totalRead;
        m_sources.update(sourceId, copy);
    }
    if (m_datasetIds.isEmpty())
    {
        m_sources.remove(sourceId);
        return fail(err::Data, QStringLiteral("没有解析出任何数据: %1").arg(origin));
    }

    // --- 7) 写缓存：把这次解析的结果存下来，下次同一份文件直接复用 ---
    //
    // 粒度按 (源文件, 解析器)：每个通道一个数据集，各存各的。
    // 指纹用源文件的 size + mtime，源文件一改缓存自动失效。
    if (options.useCache)
    {
        const QFileInfo fi(origin);
        const qint64 size = fi.size();
        const qint64 mtime = fi.lastModified().toMSecsSinceEpoch();
        for (int i = 0; i < toParse.size(); ++i)
        {
            // m_datasetIds 的顺序：缓存命中的在前、真解析的在后，
            // 这里按 toParse 的顺序对应真解析那一段的 id。
            const int idx = m_datasetIds.size() - toParse.size() + i;
            if (idx < 0 || idx >= m_datasetIds.size())
                continue;
            const Dataset *ds = m_repo.get(m_datasetIds.at(idx));
            if (!ds)
                continue;
            const QString cachePath = cachePathFor(origin, toParse.at(i));
            QString cacheErr;
            ds->saveCache(cachePath, size, mtime, &cacheErr);
            // 缓存写失败不是致命错误：只是下次要重解析。
        }
    }

    return m_datasetIds.first();
}

} // namespace nextpilot::plotscope::context
