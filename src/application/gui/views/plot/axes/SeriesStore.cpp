#include "SeriesStore.h"

namespace nextpilot::plotscope::gui
{

SeriesStore::SeriesStore(QObject *parent) : QObject(parent) {}

void SeriesStore::setColorProvider(std::function<QColor(int)> provider)
{
    m_colorProvider = std::move(provider);
}

const PlotSeries *SeriesStore::at(int index) const
{
    return index >= 0 && index < m_series.size() ? &m_series.at(index) : nullptr;
}

const PlotSeries *SeriesStore::byKey(const QString &key) const
{
    for (const PlotSeries &s : m_series)
        if (s.key() == key)
            return &s;
    return nullptr;
}

int SeriesStore::indexOf(const QString &key) const
{
    for (int i = 0; i < m_series.size(); ++i)
        if (m_series.at(i).key() == key)
            return i;
    return -1;
}

int SeriesStore::upsert(const PlotSeries &series)
{
    // 同 key 已存在则原地更新（重复拖入同一通道不叠加、只刷新数据）。
    for (int i = 0; i < m_series.size(); ++i)
    {
        if (m_series.at(i).key() == series.key())
        {
            PlotSeries updated = series;
            // 保留已有颜色（上层未显式指定时不要因刷新而换色）。
            updated.color = m_series.at(i).color;
            // 变换配方是视图态：重新取数刷新时不丢。
            updated.xTimeOffset = m_series.at(i).xTimeOffset;
            updated.yTransformId = m_series.at(i).yTransformId;
            updated.yTransformParamsJson = m_series.at(i).yTransformParamsJson;
            m_series[i] = updated;
            Q_EMIT titleChanged(updated.title, false);
            Q_EMIT changed();
            return i;
        }
    }

    PlotSeries s = series;
    if (!s.color.isValid() && m_colorProvider)
        s.color = m_colorProvider(m_series.size());
    m_series.append(s);
    const bool first = m_series.size() == 1;
    if (first)
        Q_EMIT titleChanged(s.title, true);
    Q_EMIT changed();
    return m_series.size() - 1;
}

void SeriesStore::replaceWith(const PlotSeries &series)
{
    // 兼容入口：单条曲线替换全部（旧调用方随后会喂 LOD，故不重推）。
    m_series.clear();
    PlotSeries s = series;
    if (!s.color.isValid() && m_colorProvider)
        s.color = m_colorProvider(0);
    m_series.append(s);
    Q_EMIT titleChanged(s.title, false);
}

void SeriesStore::setFirstLod(const QVector<nextpilot::plotscope::dataset::LodTriple> &lod,
                              double t0, double t1)
{
    if (m_series.isEmpty())
        return;
    m_series[0].lod = lod;
    m_series[0].t0 = t0;
    m_series[0].t1 = t1;
    m_t0 = t0;
    m_t1 = t1;
    Q_EMIT changed();
}

bool SeriesStore::remove(const QString &key)
{
    for (int i = 0; i < m_series.size(); ++i)
    {
        if (m_series.at(i).key() == key)
        {
            m_series.removeAt(i);
            Q_EMIT titleChanged(m_series.isEmpty() ? QString() : m_series.first().title, false);
            Q_EMIT changed();
            return true;
        }
    }
    return false;
}

void SeriesStore::clear()
{
    m_series.clear();
    Q_EMIT changed();
}

bool SeriesStore::setAxis(const QString &key, bool rightAxis)
{
    for (PlotSeries &s : m_series)
    {
        if (s.key() == key)
        {
            if (s.rightAxis == rightAxis)
                return true;
            s.rightAxis = rightAxis;
            Q_EMIT changed();
            return true;
        }
    }
    return false;
}

bool SeriesStore::setVisible(const QString &key, bool visible)
{
    for (PlotSeries &s : m_series)
    {
        if (s.key() == key)
        {
            if (s.visible == visible)
                return true;
            s.visible = visible;
            Q_EMIT changed();
            return true;
        }
    }
    return false;
}

void SeriesStore::setVisibleSilently(int index, bool visible)
{
    if (index < 0 || index >= m_series.size())
        return;
    m_series[index].visible = visible;
}

bool SeriesStore::setInk(const QString &key, const QColor &color)
{
    for (PlotSeries &s : m_series)
    {
        if (s.key() == key)
        {
            s.color = color;
            Q_EMIT changed();
            return true;
        }
    }
    return false;
}

bool SeriesStore::setWidth(const QString &key, double width)
{
    for (PlotSeries &s : m_series)
    {
        if (s.key() == key)
        {
            s.lineWidth = width;
            Q_EMIT changed();
            return true;
        }
    }
    return false;
}

bool SeriesStore::setXOffset(const QString &key, double offsetSec)
{
    for (PlotSeries &s : m_series)
    {
        if (s.key() == key)
        {
            // offset 是配方（绝对值），重推时对副本施加，原始数据不动。
            s.xTimeOffset = offsetSec;
            Q_EMIT changed();
            return true;
        }
    }
    return false;
}

bool SeriesStore::setYTransform(const QString &key, const QString &descriptorId,
                                const QString &paramsJson)
{
    for (PlotSeries &s : m_series)
    {
        if (s.key() == key)
        {
            s.yTransformId = descriptorId;
            s.yTransformParamsJson = paramsJson;
            // 数值施加在取数层；这里只更新配方，下次 upsert 会把新结果喂进来。
            return true;
        }
    }
    return false;
}

void SeriesStore::setSourceOffsetProvider(std::function<double(DatasetId)> provider)
{
    m_sourceOffsetProvider = std::move(provider);
}

double SeriesStore::sourceOffsetFor(const PlotSeries &s) const
{
    return m_sourceOffsetProvider ? m_sourceOffsetProvider(s.datasetId) : 0.0;
}

} // namespace nextpilot::plotscope::gui
