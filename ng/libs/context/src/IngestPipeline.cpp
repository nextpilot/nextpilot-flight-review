#include "IngestPipeline.h"

#include "Dataset.h"
#include "ParserRegistry.h"

namespace nextpilot::plotscope::context
{
using nextpilot::plotscope::parser::ParserRegistry;
using nextpilot::plotscope::dataset::ValueType;

using nextpilot::plotscope::dataset::DataRepository;
using nextpilot::plotscope::dataset::Dataset;
using nextpilot::plotscope::dataset::FrameColumn;
using nextpilot::plotscope::parser::ParserEntry;


IngestPipeline::IngestPipeline(DataRepository &repo, QObject *parent)
    : QObject(parent), m_repo(repo)
{
}

IngestPipeline::~IngestPipeline()
{
    // 销毁所有解析器实例（create/destroy 必须配对）。
    for (auto it = m_channels.begin(); it != m_channels.end(); ++it)
    {
        for (ParserChannel &ch : it.value())
        {
            if (ch.vtable && ch.ctx)
                ch.vtable->destroy(ch.ctx);
        }
    }
}

Result<int> IngestPipeline::bindParser(int sourceId, const ParserBinding &binding,
                                    DatasetId preferredDatasetId)
{
    if (binding.parserId.isEmpty())
        return fail(QStringLiteral("eformat"), QStringLiteral("解析器 id 为空"));

    const ParserEntry *entry = ParserRegistry::entry(binding.parserId);
    if (!entry || !entry->vtable)
        return fail(QStringLiteral("eformat"),
                    QStringLiteral("未注册的解析器: %1").arg(binding.parserId));
    if (entry->vtable->abi_version != PS_PARSER_ABI_VERSION)
        return fail(QStringLiteral("eabi"),
                    QStringLiteral("解析器 ABI 不匹配: %1").arg(binding.parserId));

    QVector<ParserChannel> &chans = m_channels[sourceId];
    std::vector<std::unique_ptr<SinkContext>> &ctxs = m_sinkCtx[sourceId];

    // 同一解析器在同一源上重复绑定 = 改配置（换过滤器/启停），不重建实例。
    for (int i = 0; i < chans.size(); ++i)
    {
        if (chans[i].parserId != binding.parserId)
            continue;
        chans[i].topicFilter = binding.topic;
        chans[i].active = binding.active;
        return chans[i].datasetId;
    }

    // 每个通道一个**独立数据集**：扇出的各路结果互不覆盖。
    const QString name = QStringLiteral("src%1_%2").arg(sourceId).arg(binding.parserId);
    Dataset ds = Dataset::create(name, binding.parserId);
    if (ds.isNull())
        return fail(QStringLiteral("eio"), QStringLiteral("无法创建数据集: %1").arg(name));
    // 数据集入仓：指定了稳定 id 就用它（录制/回放需要 id 不漂移），
    // 否则按仓库自增分配。
    DatasetId dsId = kInvalidDataset;
    if (preferredDatasetId != kInvalidDataset)
    {
        const Result<DatasetId> added = m_repo.addWithStableId(ds, preferredDatasetId);
        if (!added)
            return fail(added.error().code, added.error().message);
        dsId = added.value();
    }
    else
    {
        dsId = m_repo.add(ds);
        if (dsId == kInvalidDataset)
            return fail(QStringLiteral("eio"), QStringLiteral("数据集入仓失败: %1").arg(name));
    }

    ParserChannel ch;
    ch.parserId = binding.parserId;
    ch.topicFilter = binding.topic;
    ch.vtable = entry->vtable;
    ch.ctx = entry->vtable->create();
    ch.datasetId = dsId;
    ch.active = binding.active;
    if (!ch.ctx)
    {
        m_repo.remove(dsId);
        return fail(QStringLiteral("eio"),
                    QStringLiteral("解析器实例创建失败: %1").arg(binding.parserId));
    }

    // 透传解析参数给解析器（create 后、feed 前）。老解析器没有 configure 槽位，
    // ps_parser_has_configure 为假时跳过——它们用内置默认，行为不变。
    if (ps_parser_has_configure(entry->vtable) && !binding.options.isEmpty())
    {
        PsError perr{};
        const int32_t rc = entry->vtable->configure(ch.ctx, binding.options.constData(), &perr);
        if (rc != 0)
        {
            entry->vtable->destroy(ch.ctx);
            m_repo.remove(dsId);
            return fail(QStringLiteral("eopt"),
                        QStringLiteral("解析参数不被接受: %1")
                            .arg(perr.message[0] ? perr.message : binding.parserId.toUtf8().constData()));
        }
    }

    // sink 上下文与通道同寿命：回调里直接定位到数据集，不查表。
    auto ctx = std::make_unique<SinkContext>();
    ctx->pipeline = this;
    ctx->sourceId = sourceId;
    ctx->parserId = binding.parserId;
    ctx->datasetId = dsId;
    ctxs.push_back(std::move(ctx));

    chans.append(ch);
    return dsId;
}

void IngestPipeline::setTableRename(int sourceId, const QString &parserId, const QString &newName)
{
    auto cit = m_sinkCtx.find(sourceId);
    if (cit == m_sinkCtx.end())
        return;
    for (auto &ctx : cit->second)
    {
        if (!ctx || ctx->parserId != parserId)
            continue;
        ctx->tableName = newName; // 空 = 沿用解析器给出的表名
        return;
    }
}

bool IngestPipeline::unbindParser(int sourceId, const QString &parserId)
{
    auto it = m_channels.find(sourceId);
    if (it == m_channels.end())
        return false;
    auto cit = m_sinkCtx.find(sourceId);
    for (int i = 0; i < it->size(); ++i)
    {
        ParserChannel &ch = (*it)[i];
        if (ch.parserId != parserId)
            continue;
        if (ch.vtable && ch.ctx)
            ch.vtable->destroy(ch.ctx);
        it->removeAt(i);
        if (cit != m_sinkCtx.end() && i < static_cast<int>(cit->second.size()))
            cit->second[i].reset();
        return true;
    }
    return false;
}

qint64 IngestPipeline::feed(int sourceId, const uint8_t *data, qint64 size)
{
    auto it = m_channels.find(sourceId);
    if (it == m_channels.end() || !data || size <= 0)
        return 0;
    auto cit = m_sinkCtx.find(sourceId);
    if (cit == m_sinkCtx.end())
        return 0;
    const std::vector<std::unique_ptr<SinkContext>> &ctxs = cit->second;

    // 扇出：同一份字节喂给所有活跃通道。
    // 一个解析器报错只记在该通道上，不影响其它通道继续吃数据。
    qint64 consumed = 0;
    const QVector<ParserChannel> &chans = it.value();
    for (int i = 0; i < chans.size(); ++i)
    {
        ParserChannel &ch = const_cast<ParserChannel &>(chans[i]);
        if (!ch.active || !ch.vtable || !ch.ctx)
            continue;
        if (i >= static_cast<int>(ctxs.size()) || !ctxs[i])
            continue;
        IngestPipeline *self = this;

        PsParserSink sink;
        sink.ctx = ctxs[i].get();
        sink.frame_batch = &IngestPipeline::onFrameBatch;
        sink.progress = &IngestPipeline::onProgress;

        PsError err;
        err.code = 0;
        err.message[0] = '\0';

        const int32_t rc = ch.vtable->feed(ch.ctx, data, static_cast<uint32_t>(size), &sink, &err);
        if (rc != 0)
        {
            ch.error = QString::fromUtf8(err.message);
            continue; // 该通道停了，其它通道继续
        }
        ch.error.clear();
        consumed = size;
    }
    return consumed;
}

void IngestPipeline::finish(int sourceId)
{
    auto it = m_channels.find(sourceId);
    if (it == m_channels.end())
        return;
    auto cit = m_sinkCtx.find(sourceId);
    if (cit == m_sinkCtx.end())
        return;
    const std::vector<std::unique_ptr<SinkContext>> &ctxs = cit->second;

    const QVector<ParserChannel> &chans = it.value();
    for (int i = 0; i < chans.size(); ++i)
    {
        ParserChannel &ch = const_cast<ParserChannel &>(chans[i]);
        if (!ch.active || !ch.vtable || !ch.ctx)
            continue;
        if (i >= static_cast<int>(ctxs.size()) || !ctxs[i])
            continue;
        IngestPipeline *self = this;

        PsParserSink sink;
        sink.ctx = ctxs[i].get();
        sink.frame_batch = &IngestPipeline::onFrameBatch;
        sink.progress = &IngestPipeline::onProgress;

        PsError err;
        err.message[0] = '\0';
        if (ch.vtable->finish(ch.ctx, &sink, &err) != 0)
            ch.error = QString::fromUtf8(err.message);

        // 收尾：所有批次灌完后刷新一次 schema 投影（O(行数)，只做一次）。
        if (ctxs[i] && ctxs[i]->dirty)
        {
            Dataset *ds = const_cast<Dataset *>(self->m_repo.get(ctxs[i]->datasetId));
            if (ds)
                ds->refreshSchema();
            ctxs[i]->dirty = false;
        }
    }
}

QVector<IngestPipeline::ParserChannel> IngestPipeline::channels(int sourceId) const
{
    auto it = m_channels.constFind(sourceId);
    if (it == m_channels.constEnd())
        return {};
    return it.value();
}

int IngestPipeline::channelCount(int sourceId) const
{
    auto it = m_channels.constFind(sourceId);
    return it == m_channels.constEnd() ? 0 : it->size();
}

QString IngestPipeline::channelError(int sourceId, const QString &parserId) const
{
    const auto chans = channels(sourceId);
    for (const ParserChannel &ch : chans)
        if (ch.parserId == parserId)
            return ch.error;
    return QString();
}

int32_t IngestPipeline::onFrameBatch(void *ctx, const PsFrameBatch *batch)
{
    auto *sc = static_cast<SinkContext *>(ctx);
    if (!sc || !batch || batch->rows == 0)
        return 0;

    // 通过 sink 上下文反查管道（上下文里存了 pipeline 指针）。
    auto *self = sc->pipeline;
    if (!self)
        return -1;

    // 仓库的 get 返回 const 指针，但摄取管道是数据集的**写入方**：
    // 数据集在本管道内仍是可追加的（appendFrame 是非 const 方法）。
    // 这里刻意 const_cast——所有权在仓库，管道只借用。
    Dataset *ds = const_cast<Dataset *>(self->m_repo.get(sc->datasetId));
    if (!ds)
        return -1;

    // SPI 批结构与 Dataset::appendFrame 入参同构：列名/单位/类型/指针直接搬，
    // kernel 立即拷贝（C++ 侧不留副本）。
    QVector<FrameColumn> cols;
    cols.reserve(static_cast<int>(batch->columns));
    for (uint32_t i = 0; i < batch->columns; ++i)
    {
        const PsFrameColumn &c = batch->column_data[i];
        FrameColumn fc;
        fc.name = QString::fromUtf8(c.name ? c.name : "");
        fc.unit = QString::fromUtf8(c.unit ? c.unit : "");
        fc.type = static_cast<ValueType>(static_cast<int>(c.type));
        fc.values = c.values;
        fc.nulls = c.nulls;
        cols.append(fc);
    }

    // 表名改写（宿主策略）：解析器只认字节，不知道表该叫什么；
    // 单表格式由宿主命名（见 IMessageParser::preferredTableName）。
    // Rust 侧的表名在写入时定死，所以只能在这里改。
    QString table = QString::fromUtf8(batch->table ? batch->table : "");
    if (!sc->tableName.isEmpty())
        table = sc->tableName;

    QString err;
    if (!ds->appendFrame(table, batch->time_us, static_cast<int>(batch->rows), cols, &err))
        return -1;

    // 数据已进 Rust，但 C++ 侧的 schema 投影尚未刷新。
    // refreshSchema 是 O(行数)，**不能每批都调**（百万行会被拖死），
    // 所以只标记"有变更"，等通道收尾时统一刷一次。
    sc->dirty = true;
    return 0;
}

void IngestPipeline::onProgress(void *, uint64_t, uint64_t)
{
    // 进度上报预留：宿主可据此更新 UI 进度条。
}

} // namespace nextpilot::plotscope::context
