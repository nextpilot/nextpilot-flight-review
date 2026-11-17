#include "datasource/HostDataBridge.h"

#include "DataSourceRegistry.h"
#include "ps_kernel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace ps::data_source
{
namespace
{
constexpr int kDefaultMaxPoints = 4096;

// 通道时间轴是否足以支撑二分查找
bool monotonic(const PsChannelInfo *ch)
{
    if (!ch || ch->point_count < 2)
        return true;
    return ch->time[0] <= ch->time[ch->point_count - 1];
}

host::Result missing(const QString &what)
{
    return host::Result::failure(QStringLiteral("esrcgone"), what);
}
} // namespace

HostDataBridge::HostDataBridge(std::deque<DataSource> &store) : m_store(store) {}

DataSource *HostDataBridge::sourceById(int id)
{
    for (DataSource &src : m_store)
        if (src.config.id == id)
            return &src;
    return nullptr;
}

int HostDataBridge::nextId() const
{
    int id = 0;
    for (const DataSource &src : m_store)
        id = std::max(id, src.config.id + 1);
    return id;
}

QVector<host::SourceInfo> HostDataBridge::sources()
{
    QVector<host::SourceInfo> out;
    out.reserve(m_store.size());
    for (const DataSource &src : m_store)
    {
        host::SourceInfo info;
        info.id = src.config.id;
        info.name = src.config.name;
        info.alias = src.config.alias;
        info.path = src.config.path;
        info.format = src.config.fmt;
        info.live = src.isLive();
        info.paused = src.paused;
        out.append(info);
    }
    return out;
}

QVector<host::ChannelInfo> HostDataBridge::channels(int sourceId)
{
    QVector<host::ChannelInfo> out;
    DataSource *src = sourceById(sourceId);
    if (!src || src->isLive() || !src->ds)
        return out;

    const uint32_t n = ps_dataset_channel_count(src->ds);
    out.reserve(static_cast<int>(n));
    for (uint32_t i = 0; i < n; ++i)
    {
        const PsChannelInfo *ch = ps_dataset_channel(src->ds, i);
        if (!ch || !ch->name)
            continue;
        host::ChannelInfo info;
        info.name = QString::fromUtf8(ch->name);
        info.unit = ch->unit ? QString::fromUtf8(ch->unit) : QString();
        info.count = ch->point_count;
        if (ch->point_count > 0)
        {
            info.t0 = ch->time[0];
            info.t1 = ch->time[ch->point_count - 1];
            double lo = ch->value[0];
            double hi = lo;
            for (uint32_t k = 1; k < ch->point_count; ++k)
            {
                lo = std::min(lo, ch->value[k]);
                hi = std::max(hi, ch->value[k]);
            }
            info.vMin = lo;
            info.vMax = hi;
        }
        out.append(info);
    }
    return out;
}

host::Result HostDataBridge::seriesView(int sourceId, const QString &channel,
                                              SeriesView *out)
{
    if (!out)
        return host::Result::failure(QStringLiteral("einvalid"), QStringLiteral("输出指针为空"));

    DataSource *src = sourceById(sourceId);
    if (!src)
        return missing(QStringLiteral("数据源不存在：%1").arg(sourceId));
    if (!src->ds)
        return missing(QStringLiteral("数据源没有数据集：%1").arg(sourceId));

    const uint32_t n = ps_dataset_channel_count(src->ds);
    for (uint32_t i = 0; i < n; ++i)
    {
        const PsChannelInfo *c = ps_dataset_channel(src->ds, i);
        if (c && c->name && channel == QString::fromUtf8(c->name))
        {
            out->time = c->time;
            out->value = c->value;
            out->count = c->point_count;
            return {};
        }
    }
    return host::Result::failure(QStringLiteral("enoent"),
                                      QStringLiteral("通道不存在：%1").arg(channel));
}

host::Result HostDataBridge::slice(int sourceId, const QString &channel, double t0, double t1,
                                          int maxPoints, QVector<double> *t, QVector<double> *v)
{
    if (!t || !v)
        return host::Result::failure(QStringLiteral("einvalid"), QStringLiteral("输出指针为空"));

    DataSource *src = sourceById(sourceId);
    if (!src)
        return missing(QStringLiteral("数据源不存在：%1").arg(sourceId));
    if (src->isLive())
        return host::Result::failure(QStringLiteral("elive"),
                                         QStringLiteral("实时源没有历史切片"));
    if (!src->ds)
        return missing(QStringLiteral("数据源没有数据集：%1").arg(sourceId));

    const uint32_t n = ps_dataset_channel_count(src->ds);
    const PsChannelInfo *ch = nullptr;
    for (uint32_t i = 0; i < n; ++i)
    {
        const PsChannelInfo *c = ps_dataset_channel(src->ds, i);
        if (c && c->name && channel == QString::fromUtf8(c->name))
        {
            ch = c;
            break;
        }
    }
    if (!ch)
        return host::Result::failure(QStringLiteral("enoent"),
                                         QStringLiteral("通道不存在：%1").arg(channel));
    if (ch->point_count == 0)
    {
        t->clear();
        v->clear();
        return {};
    }

    const int want = maxPoints > 0 ? maxPoints : kDefaultMaxPoints;
    const double lo = std::min(t0, t1);
    const double hi = std::max(t0, t1);

    uint32_t i0 = 0;
    uint32_t i1 = ch->point_count;
    if (monotonic(ch))
    {
        i0 = static_cast<uint32_t>(
            std::lower_bound(ch->time, ch->time + ch->point_count, lo) - ch->time);
        i1 = static_cast<uint32_t>(
            std::upper_bound(ch->time, ch->time + ch->point_count, hi) - ch->time);
    }
    if (i1 <= i0)
    {
        t->clear();
        v->clear();
        return {};
    }

    const uint32_t span = i1 - i0;
    const uint32_t stride = span > static_cast<uint32_t>(want)
                                ? (span + static_cast<uint32_t>(want) - 1) / static_cast<uint32_t>(want)
                                : 1;
    t->clear();
    v->clear();
    t->reserve(static_cast<int>((span + stride - 1) / stride));
    v->reserve(t->capacity());
    for (uint32_t k = i0; k < i1; k += stride)
    {
        t->append(ch->time[k]);
        v->append(ch->value[k]);
    }
    return {};
}

host::LiveStatus HostDataBridge::liveStatus(int sourceId)
{
    host::LiveStatus st;
    const DataSource *src = sourceById(sourceId);
    if (!src || !src->isLive())
        return st;
    st.connected = src->handle != nullptr;
    // recording / dropped / samples 由 LiveSession 在 S4 补齐，S1 不猜字段
    return st;
}

host::JobHandle HostDataBridge::loadFile(const QString &path, const QString &loaderOptions)
{
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile())
        return {};

    const DataSourceRegistry &reg = DataSourceRegistry::instance();
    const DataSourceEntry *entry = nullptr;
    if (!loaderOptions.isEmpty())
    {
        // 显式 loader id 时优先按 id 找
        entry = reg.findFileById(loaderOptions);
        if (!entry)
            entry = reg.findFileBySuffix(fi.suffix());
    }
    else
    {
        entry = reg.findFileBySuffix(fi.suffix());
    }
    if (!entry || !entry->load)
        return {};

    const QString opts = entry->defaultOptions ? entry->defaultOptions(path) : QStringLiteral("{}");
    PsDataset *ds = nullptr;
    char err[512] = {0};
    if (entry->load(path, opts, &ds, err, sizeof(err)) != 0 || !ds)
        return {};

    DataSource src;
    src.config.id = nextId();
    src.config.name = fi.fileName();
    src.config.alias = fi.completeBaseName();
    src.config.path = fi.absoluteFilePath();
    src.config.fmt = entry->formatTag.isEmpty() ? entry->id : entry->formatTag;
    src.ds = ds;
    src.channels.clear();
    const uint32_t n = ps_dataset_channel_count(ds);
    for (uint32_t i = 0; i < n; ++i)
        src.channels.append(ps_dataset_channel(ds, i));
    m_store.push_back(std::move(src));
    return {m_store.back().config.id};
}

host::Result HostDataBridge::closeSource(int sourceId)
{
    for (int i = 0; i < m_store.size(); ++i)
    {
        if (m_store[i].config.id != sourceId)
            continue;
        DataSource &src = m_store[i];
        if (src.ds)
            ps_dataset_free(src.ds);
        src.ds = nullptr;
        m_store.erase(m_store.begin() + i);
        return {};
    }
    return missing(QStringLiteral("数据源不存在：%1").arg(sourceId));
}

host::JobHandle HostDataBridge::reloadSource(int sourceId)
{
    DataSource *src = sourceById(sourceId);
    if (!src || src->config.path.isEmpty())
        return {};
    const QString path = src->config.path;
    if (src->ds)
        ps_dataset_free(src->ds);
    src->ds = nullptr;
    const QFileInfo fi(path);
    const DataSourceRegistry &reg = DataSourceRegistry::instance();
    const DataSourceEntry *entry = reg.findFileBySuffix(fi.suffix());
    if (!entry || !entry->load)
        return {};
    const QString opts = entry->defaultOptions ? entry->defaultOptions(path) : QStringLiteral("{}");
    PsDataset *ds = nullptr;
    char err[512] = {0};
    if (entry->load(path, opts, &ds, err, sizeof(err)) != 0 || !ds)
        return {};
    src->ds = ds;
    src->channels.clear();
    const uint32_t n = ps_dataset_channel_count(ds);
    for (uint32_t i = 0; i < n; ++i)
        src->channels.append(ps_dataset_channel(ds, i));
    return {src->config.id};
}

host::JobHandle HostDataBridge::exportCsv(int sourceId, const QString &path, bool overwrite)
{
    DataSource *src = sourceById(sourceId);
    if (!src || !src->ds)
        return {};

    const QFileInfo out(path);
    if (out.exists() && !overwrite)
        return {};
    QDir().mkpath(out.absolutePath());

    const uint32_t n = ps_dataset_channel_count(src->ds);
    if (n == 0)
        return {};

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    QTextStream ts(&f);

    // 宽表：以最长通道为准，其它通道按同索引对齐，缺失留空
    uint32_t rows = 0;
    for (uint32_t i = 0; i < n; ++i)
    {
        const PsChannelInfo *ch = ps_dataset_channel(src->ds, i);
        if (ch)
            rows = std::max(rows, ch->point_count);
    }

    ts << "time";
    for (uint32_t i = 0; i < n; ++i)
    {
        const PsChannelInfo *ch = ps_dataset_channel(src->ds, i);
        ts << ',' << (ch && ch->name ? QString::fromUtf8(ch->name) : QStringLiteral("ch%1").arg(i));
    }
    ts << '\n';

    for (uint32_t r = 0; r < rows; ++r)
    {
        double t = 0.0;
        bool haveT = false;
        for (uint32_t i = 0; i < n && !haveT; ++i)
        {
            const PsChannelInfo *ch = ps_dataset_channel(src->ds, i);
            if (ch && r < ch->point_count)
            {
                t = ch->time[r];
                haveT = true;
            }
        }
        ts << QString::number(t, 'g', 17);
        for (uint32_t i = 0; i < n; ++i)
        {
            const PsChannelInfo *ch = ps_dataset_channel(src->ds, i);
            ts << ',';
            if (ch && r < ch->point_count)
                ts << QString::number(ch->value[r], 'g', 17);
        }
        ts << '\n';
    }
    if (!f.commit())
        return {};
    return {sourceId};
}

host::JobHandle HostDataBridge::connectLive(const QString &, const QString &)
{
    return {};
}

host::Result HostDataBridge::disconnectLive(int sourceId)
{
    return host::Result::failure(QStringLiteral("eunsupported"),
                                      QStringLiteral("S1 不接实时源"));
}

host::Result HostDataBridge::setPaused(int sourceId, bool paused)
{
    DataSource *src = sourceById(sourceId);
    if (!src || !src->isLive())
        return missing(QStringLiteral("实时源不存在：%1").arg(sourceId));
    src->paused = paused;
    return {};
}

host::Result HostDataBridge::startRecording(int sourceId, const QString &, const QString &)
{
    if (!sourceById(sourceId))
        return missing(QStringLiteral("数据源不存在：%1").arg(sourceId));
    return host::Result::failure(QStringLiteral("eunsupported"), QStringLiteral("S1 不接录制"));
}

host::Result HostDataBridge::stopRecording(int sourceId)
{
    if (!sourceById(sourceId))
        return missing(QStringLiteral("数据源不存在：%1").arg(sourceId));
    return host::Result::failure(QStringLiteral("eunsupported"), QStringLiteral("S1 不接录制"));
}

} // namespace ps::data_source
