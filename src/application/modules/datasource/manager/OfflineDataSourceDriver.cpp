#include "OfflineDataSourceDriver.h"

#include "DatasetCache.h"
#include "DataSourceEntry.h"
#include "DataSourceModel.h"
#include "DataSourceRegistry.h"
#include "HostApi.h"
#include "ps_kernel.h"

namespace ps::data_source
{
namespace
{
struct ProgressCtx
{
    const OfflineDataSourceDriver::ProgressFn *fn = nullptr;
};

void onProgress(void *ctx, uint64_t done, uint64_t total)
{
    auto *p = static_cast<ProgressCtx *>(ctx);
    if (p && p->fn && *p->fn)
        (*p->fn)(done, total);
}
} // namespace

OfflineDataSourceDriver::OfflineDataSourceDriver(PsDataset *ds) : m_ds(ds) {}

OfflineDataSourceDriver::~OfflineDataSourceDriver()
{
    if (m_ds)
        ps_dataset_free(m_ds);
}

PsDataset *OfflineDataSourceDriver::load(const DataSourceConfig &config, const QString &optionJson,
                                         bool *cacheHit, QString *err, const ProgressFn &progress)
{
    if (cacheHit)
        *cacheHit = false;
    const DataSourceEntry *entry = DataSourceRegistry::instance().findSourceById(config.extensionId);
    if (!entry || !entry->load)
    {
        if (err)
            *err = QStringLiteral("没有匹配的离线加载器：%1").arg(config.extensionId);
        return nullptr;
    }

    if (entry->cacheable)
    {
        if (PsDataset *cached = DatasetCache::load(config.path, entry->cacheNamespace, optionJson))
        {
            if (cacheHit)
                *cacheHit = true;
            return cached;
        }
    }

    ProgressCtx ctx{&progress};
    if (progress)
        ps_host_progress_begin(&ctx, &onProgress);
    char e[256] = {0};
    PsDataset *ds = nullptr;
    const int rc = entry->load(config.path, optionJson, &ds, e, sizeof(e));
    if (progress)
        ps_host_progress_end();
    if (rc != 0 || !ds)
    {
        if (err)
            *err = QString::fromUtf8(e).isEmpty()
                ? QStringLiteral("加载失败：%1").arg(config.path)
                : QString::fromUtf8(e);
        return nullptr;
    }

    if (entry->cacheable)
        DatasetCache::save(config.path, entry->cacheNamespace, optionJson, ds);
    return ds;
}

void OfflineDataSourceDriver::adopt(PsDataset *ds)
{
    if (m_ds && m_ds != ds)
        ps_dataset_free(m_ds);
    m_ds = ds;
}

bool OfflineDataSourceDriver::open(DataSource &src, QString *err)
{
    if (!m_ds && src.ds)
        m_ds = src.ds;
    if (m_ds)
    {
        src.ds = m_ds;
        return true;
    }
    src.ds = load(src.config, src.config.configJson, nullptr, err);
    if (!src.ds)
        return false;
    m_ds = src.ds;
    const uint32_t n = ps_dataset_channel_count(m_ds);
    src.channels.clear();
    for (uint32_t i = 0; i < n; ++i)
        if (const PsChannelInfo *ch = ps_dataset_channel(m_ds, i))
            src.channels.append(ch);
    return true;
}

void OfflineDataSourceDriver::close(DataSource &src)
{
    ++src.generation;
    if (m_ds)
        ps_dataset_free(m_ds);
    m_ds = nullptr;
    src.ds = nullptr;
    src.channels.clear();
}

bool OfflineDataSourceDriver::poll(DataSource &, QString *)
{
    return true;
}

bool OfflineDataSourceDriver::reload(DataSource &src, QString *err)
{
    bool cacheHit = false;
    PsDataset *next = load(src.config, src.config.configJson, &cacheHit, err);
    if (!next)
        return false;
    adopt(next);
    ++src.generation;
    src.ds = next;
    src.channels.clear();
    const uint32_t n = ps_dataset_channel_count(next);
    for (uint32_t i = 0; i < n; ++i)
        if (const PsChannelInfo *ch = ps_dataset_channel(next, i))
            src.channels.append(ch);
    return true;
}

} // namespace ps::data_source
