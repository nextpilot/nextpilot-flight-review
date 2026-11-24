#include "CursorReadout.h"

#include <cmath>

#include <fastplot/Axes.h>
#include <fastplot/Viewport.h>

#include "SeriesStore.h"

namespace nextpilot::plotscope::gui
{

CursorReadout::CursorReadout(SeriesStore *store, QObject *parent)
    : QObject(parent), m_store(store)
{
}

void CursorReadout::setPlot(nextpilot::plotscope::fastplot::Axes *plot)
{
    m_plot = plot;
}

CursorReadout::SampledValue CursorReadout::sample(const PlotSeries &s, double t) const
{
    SampledValue out;
    if (s.lod.isEmpty())
        return out;
    // LOD 三元组按 x 升序，二分找最近列。x 已含 xTimeOffset 与源 offset，故目标要加偏移。
    const double target =
        t + s.xTimeOffset + (m_store ? m_store->sourceOffsetFor(s) : 0.0);
    const QVector<nextpilot::plotscope::dataset::LodTriple> &lod = s.lod;
    int lo = 0, hi = lod.size() - 1;
    while (lo < hi)
    {
        const int mid = (lo + hi) / 2;
        if (lod.at(mid).x < target)
            lo = mid + 1;
        else
            hi = mid;
    }
    int idx = lo;
    if (idx > 0 && std::fabs(lod.at(idx - 1).x - target) <= std::fabs(lod.at(idx).x - target))
        --idx;
    const nextpilot::plotscope::dataset::LodTriple &p = lod.at(idx);
    out.ok = true;
    out.value = (p.min + p.max) / 2.0; // LOD 桶的代表值（与 fastplot nearestSample 一致）
    return out;
}

void CursorReadout::refresh(const QVector<Mark> &marks)
{
    if (!m_plot || !m_store)
        return;

    // 十字游标读数框。
    if (m_active)
    {
        QVector<fastplot::Axes::ReadoutRow> rows;
        for (const PlotSeries &s : m_store->all())
        {
            if (!s.visible || rows.size() >= 8)
                continue;
            const SampledValue v = sample(s, m_t);
            rows.append({s.legendName(),
                         v.ok ? QString::number(v.value, 'g', 6) : QStringLiteral("--"),
                         s.color});
        }
        // 参考线相对 Δ（第3仗 3c / F-2）：参考线激活时标题栏追加 "Δ +x.xxx s"。
        QString header = m_pinned ? QStringLiteral("固定 t = %1 s").arg(m_t, 0, 'f', 3)
                                  : QStringLiteral("t = %1 s").arg(m_t, 0, 'f', 3);
        if (m_plot->hasReference())
        {
            const double delta = m_t - m_plot->referenceTime();
            header += QStringLiteral("   Δ %1%2 s")
                          .arg(delta >= 0 ? QStringLiteral("+") : QStringLiteral("-"))
                          .arg(std::fabs(delta), 0, 'f', 3);
        }
        m_plot->setCursorReadout(m_t, true, m_pinned, header, rows);
    }
    else
    {
        m_plot->setCursorReadout(0.0, false, false, QString(), {});
    }

    // A/B 读数：Δt + 每曲线 Δy 与斜率。
    if (m_abEnabled)
    {
        const double dt = m_abB - m_abA;
        QVector<fastplot::Axes::ReadoutRow> rows;
        bool triangle = false;
        double ayPix = 0.0, byPix = 0.0;
        for (const PlotSeries &s : m_store->all())
        {
            if (!s.visible || rows.size() >= 6)
                continue;
            const SampledValue va = sample(s, m_abA);
            const SampledValue vb = m_abHasB ? sample(s, m_abB) : SampledValue{};
            const QString dy = (va.ok && vb.ok)
                                   ? QString::number(vb.value - va.value, 'g', 6)
                                   : QStringLiteral("--");
            const QString k = (va.ok && vb.ok && std::fabs(dt) > 1e-12)
                                  ? QString::number((vb.value - va.value) / dt, 'g', 5)
                                        + QStringLiteral("/s")
                                  : QStringLiteral("--");
            rows.append({s.legendName(), QStringLiteral("Δy %1  k %2").arg(dy, k), s.color});
            // 首条可见曲线画直角三角形：把 A/B 两点 y 数据换算成像素。
            if (!triangle && va.ok && vb.ok && s.rightAxis == false)
            {
                const QRectF plot = m_plot->plotRect();
                const double lo = m_plot->viewport().yLower();
                const double hi = m_plot->viewport().yUpper();
                if (hi > lo)
                {
                    ayPix = plot.bottom() - plot.height() * (va.value - lo) / (hi - lo);
                    byPix = plot.bottom() - plot.height() * (vb.value - lo) / (hi - lo);
                    triangle = true;
                }
            }
        }
        const QString header = QStringLiteral("Δx = %1 s（时间轴）").arg(dt, 0, 'f', 4);
        m_plot->setAB(true, m_abA, m_abB, m_abHasB, header, rows, triangle, ayPix, byPix, 0.0);
    }
    else
    {
        m_plot->setAB(false, 0, 0, false, QString(), {}, false, 0, 0, 0.0);
    }

    // 书签。
    QVector<fastplot::Axes::BookmarkMark> bmarks;
    for (const Mark &m : marks)
    {
        bmarks.append({m.id, m.t,
                       m.label.trimmed().isEmpty()
                           ? QStringLiteral("t=%1").arg(m.t, 0, 'f', 3)
                           : m.label,
                       m.selected});
    }
    m_plot->setBookmarks(bmarks);
}

} // namespace nextpilot::plotscope::gui
