#include "datasource/ScriptHostBridge.h"

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

scripting::Result missing(const QString &what)
{
    return scripting::Result::failure(QStringLiteral("esrcgone"), what);
}
} // namespace

ScriptHostBridge::ScriptHostBridge(SourceStore &store) : m_store(store) {}

QVector<scripting::SourceInfo> ScriptHostBridge::sources()
{
    QVector<scripting::SourceInfo> out;
    out.reserve(m_store.items().size());
    for (const SourceData &src : m_store.items())
    {
        scripting::SourceInfo info;
        info.id = src.id;
        info.name = src.name;
        info.alias = src.alias;
        info.path = src.path;
        info.format = src.fmt;
        info.live = src.live;
        info.paused = src.paused;
        out.append(info);
    }
    return out;
}

QVector<scripting::ChannelInfo> ScriptHostBridge::channels(int sourceId)
{
    QVector<scripting::ChannelInfo> out;
    SourceData *src = m_store.byId(sourceId);
    if (!src || src->live || !src->ds)
        return out;

    const uint32_t n = ps_dataset_channel_count(src->ds);
    out.reserve(static_cast<int>(n));
    for (uint32_t i = 0; i < n; ++i)
    {
        const PsChannelInfo *ch = ps_dataset_channel(src->ds, i);
        if (!ch || !ch->name)
            continue;
        scripting::ChannelInfo info;
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

scripting::Result ScriptHostBridge::slice(int sourceId, const QString &channel, double t0, double t1,
                                          int maxPoints, QVector<double> *t, QVector<double> *v)
{
    if (!t || !v)
        return scripting::Result::failure(QStringLiteral("einvalid"), QStringLiteral("输出指针为空"));

    SourceData *src = m_store.byId(sourceId);
    if (!src)
        return missing(QStringLiteral("数据源不存在：%1").arg(sourceId));
    if (src->live)
        return scripting::Result::failure(QStringLiteral("elive"),
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
        return scripting::Result::failure(QStringLiteral("enoent"),
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

scripting::LiveStatus ScriptHostBridge::liveStatus(int sourceId)
{
    scripting::LiveStatus st;
    const SourceData *src = m_store.byId(sourceId);
    if (!src || !src->live)
        return st;
    st.connected = src->handle != nullptr;
    // recording / dropped / samples 由 LiveSession 在 S4 补齐，S1 不猜字段
    return st;
}

scripting::JobHandle ScriptHostBridge::loadFile(const QString &path, const QString &loaderOptions)
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

    SourceData src;
    src.id = m_store.nextId();
    src.name = fi.fileName();
    src.alias = fi.completeBaseName();
    src.path = fi.absoluteFilePath();
    src.fmt = entry->formatTag.isEmpty() ? entry->id : entry->formatTag;
    src.ds = ds;
    src.channels.clear();
    const uint32_t n = ps_dataset_channel_count(ds);
    for (uint32_t i = 0; i < n; ++i)
        src.channels.append(ps_dataset_channel(ds, i));
    m_store.items().append(src);
    return {src.id};
}

scripting::Result ScriptHostBridge::closeSource(int sourceId)
{
    for (int i = 0; i < m_store.items().size(); ++i)
    {
        if (m_store.items()[i].id != sourceId)
            continue;
        SourceData &src = m_store.items()[i];
        if (src.ds)
            ps_dataset_free(src.ds);
        src.ds = nullptr;
        m_store.items().removeAt(i);
        return {};
    }
    return missing(QStringLiteral("数据源不存在：%1").arg(sourceId));
}

scripting::JobHandle ScriptHostBridge::reloadSource(int sourceId)
{
    SourceData *src = m_store.byId(sourceId);
    if (!src || src->path.isEmpty())
        return {};
    const QString path = src->path;
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
    return {src->id};
}

scripting::JobHandle ScriptHostBridge::exportCsv(int sourceId, const QString &path, bool overwrite)
{
    SourceData *src = m_store.byId(sourceId);
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

scripting::JobHandle ScriptHostBridge::connectLive(const QString &, const QString &)
{
    return {};
}

scripting::Result ScriptHostBridge::disconnectLive(int sourceId)
{
    return scripting::Result::failure(QStringLiteral("eunsupported"),
                                      QStringLiteral("S1 不接实时源"));
}

scripting::Result ScriptHostBridge::setPaused(int sourceId, bool paused)
{
    SourceData *src = m_store.byId(sourceId);
    if (!src || !src->live)
        return missing(QStringLiteral("实时源不存在：%1").arg(sourceId));
    src->paused = paused;
    return {};
}

scripting::Result ScriptHostBridge::startRecording(int sourceId, const QString &, const QString &)
{
    if (!m_store.byId(sourceId))
        return missing(QStringLiteral("数据源不存在：%1").arg(sourceId));
    return scripting::Result::failure(QStringLiteral("eunsupported"), QStringLiteral("S1 不接录制"));
}

scripting::Result ScriptHostBridge::stopRecording(int sourceId)
{
    if (!m_store.byId(sourceId))
        return missing(QStringLiteral("数据源不存在：%1").arg(sourceId));
    return scripting::Result::failure(QStringLiteral("eunsupported"), QStringLiteral("S1 不接录制"));
}

} // namespace ps::data_source
