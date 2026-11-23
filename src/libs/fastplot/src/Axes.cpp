#include "fastplot/Axes.h"
#include "fastplot/Bar.h"
#include "fastplot/Histogram.h"
#include "fastplot/Spectrum.h"
#include "fastplot/StateColors.h"

#include <QFile>
#include <QFontMetrics>
#include <QJsonArray>
#include <QPainter>
#include <QTextStream>
#include <QPolygonF>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace nextpilot::plotscope::fastplot
{

namespace
{
int sideIndex(AxisSide side)
{
    switch (side)
    {
    case AxisSide::Left: return 0;
    case AxisSide::Right: return 1;
    case AxisSide::Bottom: return 2;
    case AxisSide::Top: return 3;
    }
    return 0;
}
} // namespace

Axes::Axes(QWidget *parent)
    : QWidget(parent)
    , m_axes{Axis(AxisSide::Left), Axis(AxisSide::Right), Axis(AxisSide::Bottom), Axis(AxisSide::Top)}
{
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    // Legend::collapseChanged → 重绘 + 转发为 Axes::legendCollapseChanged（对外信号不变）。
    connect(&m_legend, &Legend::collapseChanged, this, [this](bool collapsed) {
        update();
        Q_EMIT legendCollapseChanged(collapsed);
    });

    // P1 默认只要左轴 + 下轴；右轴/上轴按需 setVisible(true)。
    m_axes[sideIndex(AxisSide::Left)].setVisible(true);
    m_axes[sideIndex(AxisSide::Bottom)].setVisible(true);
    m_axes[sideIndex(AxisSide::Right)].setVisible(false);
    m_axes[sideIndex(AxisSide::Top)].setVisible(false);

    m_vp.setXRange(0.0, 1.0);
    m_vp.setYRange(0.0, 1.0);
    relayout();

    // 网格开关的"真值"在两处：Axes::m_grid 与各 Axis::m_grid。
    // Axis 默认 grid=true、Axes 默认 false，绘制走的是 Axis —— 于是
    // 首次打开菜单会出现"画布上有网格、菜单里没勾"的不一致。这里以 Axis 为准对齐。
    m_grid = m_axes[sideIndex(AxisSide::Left)].gridVisible();
}

Axes::~Axes() = default;

Axis *Axes::axisPtr(AxisSide side)
{
    return &m_axes[sideIndex(side)];
}

Axis &Axes::axis(AxisSide side)
{
    return *axisPtr(side);
}

const Axis &Axes::axis(AxisSide side) const
{
    return m_axes[sideIndex(side)];
}

void Axes::setYRange2(double lo, double hi)
{
    // 与左轴同一套规整规则（顺序、单点展开）。
    if (lo > hi)
        std::swap(lo, hi);
    if (lo == hi)
    {
        const double pad = std::fabs(lo) > 1.0 ? std::fabs(lo) * 0.05 : 1.0;
        lo -= pad;
        hi += pad;
    }
    m_y2Lo = lo;
    m_y2Hi = hi;
    relayout();
}

void Axes::setSecondAxis(bool enabled)
{
    m_secondAxis = enabled;
    m_axes[sideIndex(AxisSide::Right)].setVisible(enabled);
    relayout();
}

void Axes::setLegendVisible(bool visible)
{
    m_legend.setVisible(visible);
    update();
}

double Axes::yToPixel(AxisSide side, double y) const
{
    const QRectF plot = m_vp.plotRect();
    if (side == AxisSide::Right && m_secondAxis)
        return plot.bottom() - (y - m_y2Lo) / (m_y2Hi - m_y2Lo) * plot.height();
    return m_vp.yToPixel(y);
}

bool Axes::exportPng(const QString &path)
{
    // 导出不带悬停浮层：读数/A-B 浮窗跟着鼠标走，grab 时鼠标恰好停在画布上
    // 就会被一并画进图（实测截图里出现过 t/v 读数框）。存下状态、导出、还原。
    const bool cursorWas = m_cursorReadout.isActive();
    const QVector<ReadoutRow> abRowsWas = m_abMeasure.rows();
    m_cursorReadout.setActive(false);
    m_abMeasure.setRows({});
    const bool ok = grabImage().save(path);
    m_cursorReadout.setActive(cursorWas);
    m_abMeasure.setRows(abRowsWas);
    update();
    return ok;
}

bool Axes::exportCsv(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream out(&file);
    out << "# fastplot\n";
    out << "# x_lo=" << QString::number(m_vp.xLower(), 'g', 12)
        << " x_hi=" << QString::number(m_vp.xUpper(), 'g', 12)
        << " y_lo=" << QString::number(m_vp.yLower(), 'g', 12)
        << " y_hi=" << QString::number(m_vp.yUpper(), 'g', 12) << "\n";

    for (const Line &c : m_curves)
    {
        if (!c.isVisible())
            continue;
        out << "# curve " << (c.name().isEmpty() ? QStringLiteral("unnamed") : c.name()) << "\n";
        if (c.isLod())
        {
            out << "x,min,max\n";
            for (const LodPoint &pt : c.m_lod)
                out << QString::number(pt.x, 'g', 12) << ","
                    << QString::number(pt.min, 'g', 12) << ","
                    << QString::number(pt.max, 'g', 12) << "\n";
        }
        else if (c.m_data && c.m_data->size() > 0)
        {
            out << "x,y\n";
            for (qsizetype i = 0; i < c.m_data->size(); ++i)
                out << QString::number(c.m_data->xAt(i), 'g', 12) << ","
                    << QString::number(c.m_data->yAt(i), 'g', 12) << "\n";
        }
    }
    return true;
}

QJsonObject Axes::saveState() const
{
    // 只存"视图"，不存数据：数据归宿主，画布状态归画布。
    QJsonObject o;
    o[QStringLiteral("x")] = QJsonArray{m_vp.xLower(), m_vp.xUpper()};
    o[QStringLiteral("y")] = QJsonArray{m_vp.yLower(), m_vp.yUpper()};
    o[QStringLiteral("y2")] = QJsonArray{m_y2Lo, m_y2Hi};
    o[QStringLiteral("secondAxis")] = m_secondAxis;
    o[QStringLiteral("topAxis")] = m_axes[sideIndex(AxisSide::Top)].isVisible();
    o[QStringLiteral("background")] = m_background.name(QColor::HexRgb);
    o[QStringLiteral("grid")] = m_axes[sideIndex(AxisSide::Left)].gridVisible();
    o[QStringLiteral("box")] = m_box;
    o[QStringLiteral("legend")] = m_legend.isVisible();
    o[QStringLiteral("legendCollapsed")] = m_legend.isCollapsed();   // #205
    o[QStringLiteral("legendFontScale")] = m_legend.fontScale();   // #205
    return o;
}

bool Axes::restoreState(const QJsonObject &state)
{
    const QJsonArray x = state.value(QStringLiteral("x")).toArray();
    const QJsonArray y = state.value(QStringLiteral("y")).toArray();
    if (x.size() == 2 && x.at(0).isDouble() && x.at(1).isDouble())
        setXRange(x.at(0).toDouble(), x.at(1).toDouble());
    if (y.size() == 2)
        setYRange(y.at(0).toDouble(), y.at(1).toDouble());

    const QJsonArray y2 = state.value(QStringLiteral("y2")).toArray();
    if (y2.size() == 2)
        setYRange2(y2.at(0).toDouble(), y2.at(1).toDouble());

    setSecondAxis(state.value(QStringLiteral("secondAxis")).toBool(false));
    m_axes[sideIndex(AxisSide::Top)].setVisible(state.value(QStringLiteral("topAxis")).toBool(false));
    const QString bg = state.value(QStringLiteral("background")).toString();
    if (!bg.isEmpty())
    {
        QColor color(bg);
        if (color.isValid())
            setBackground(color);
    }
    setGridVisible(state.value(QStringLiteral("grid")).toBool(true));
    setBoxVisible(state.value(QStringLiteral("box")).toBool(false));
    setLegendVisible(state.value(QStringLiteral("legend")).toBool(false));
    setLegendCollapsed(state.value(QStringLiteral("legendCollapsed")).toBool(false)); // #205
    {
        const double scale = state.value(QStringLiteral("legendFontScale")).toDouble(1.0);
        if (scale > 0)
        {
            m_legend.setFontScale(scale);
            update();
        }
    }
    relayout();
    return true;
}

void Axes::setXRange(double lo, double hi)
{
    m_vp.setXRange(lo, hi);
    relayout();
}

void Axes::setYRange(double lo, double hi)
{
    m_vp.setYRange(lo, hi);
    relayout();
}

void Axes::setBackground(const QColor &color)
{
    m_background = color;
    invalidateStatic();
}

void Axes::setTitle(const QString &title)
{
    if (m_title == title)
        return;
    m_title = title;
    relayout();   // 标题占一行，边距要重算
}

void Axes::setTitleColor(const QColor &color)
{
    m_titleColor = color;
    invalidateStatic();
}

void Axes::setAxisGridVisible(AxisSide side, bool visible)
{
    m_axes[sideIndex(side)].setGridVisible(visible);
    replot();
}

bool Axes::axisGridVisible(AxisSide side) const
{
    return m_axes[sideIndex(side)].gridVisible();
}

void Axes::setGridVisible(bool visible)
{
    m_grid = visible;
    for (Axis &a : m_axes)
        a.setGridVisible(visible);
    invalidateStatic();
}

void Axes::setBoxVisible(bool visible)
{
    if (m_box == visible)
        return;
    m_box = visible;
    invalidateStatic();
}

double Axes::pixelToX(double px) const
{
    return m_vp.pixelToX(px);
}

double Axes::xToPixel(double x) const
{
    return m_vp.toPixel(x, m_vp.yLower()).x();
}

void Axes::setCursorReadout(double x, bool active, bool pinned,
                            const QString &header, const QVector<ReadoutRow> &rows)
{
    m_cursorReadout.set(x, active, pinned, header, rows);
    update();
}

void Axes::setAB(bool enabled, double a, double b, bool hasB,
                 const QString &header, const QVector<ReadoutRow> &rows,
                 bool drawTriangle, double ay, double by, double ayPixelData)
{
    m_abMeasure.set(enabled, a, b, hasB, header, rows, drawTriangle, ay, by);
    (void)ayPixelData; // 预留：A 点的 y 像素，供未来精确画直角顶点
    markOverlayDirty(OverlayLayerABMeasure);
    update();
}

void Axes::setBookmarks(const QVector<BookmarkMark> &marks)
{
    m_bookmarkOverlay.setMarks(marks);
    markOverlayDirty(OverlayLayerBookmark);
    update();
}

void Axes::setMarkers(const QVector<MarkerMark> &marks)
{
    m_markerOverlay.setMarks(marks);
    markOverlayDirty(OverlayLayerMarker);
    update();
}

void Axes::setStateBands(const QVector<StateRowMark> &rows)
{
    if (rows.size() == m_stateBandOverlay.rowCount() && rows == m_stateBandOverlay.rows())
        return; // 内容不变不重排（relayout 有开销）
    m_stateBandOverlay.setRows(rows);
    markOverlayDirty(OverlayLayerStateBand);
    relayout(); // 顶部为状态行预留空间（行高 + 行名）
    update();
}

bool Axes::nearestSample(const QPointF &pixel, double *x, double *y) const
{
    const QPointF data = QPointF(m_vp.pixelToX(pixel.x()), m_vp.pixelToY(pixel.y()));
    if (x)
        *x = data.x();
    if (y)
        *y = data.y();

    for (const Line &c : m_curves)
    {
        if (!c.isVisible())
            continue;
        if (c.isLod())
        {
            // LOD 点按 x 升序：二分找最近列，取该列的 (min+max)/2。
            if (c.m_lod.isEmpty())
                continue;
            int lo = 0, hi = c.m_lod.size() - 1;
            while (lo < hi)
            {
                const int mid = (lo + hi) / 2;
                if (c.m_lod.at(mid).x < data.x())
                    lo = mid + 1;
                else
                    hi = mid;
            }
            const LodPoint &pt = c.m_lod.at(lo);
            if (x)
                *x = pt.x;
            if (y)
                *y = (pt.min + pt.max) / 2.0;
            return true;
        }
        const IPlotData *d = c.m_data;
        if (!d || d->size() <= 0)
            continue;
        // 提示路径（悬停读数）：数据量大时按步长抽样，不在鼠标移动里扫全量。
        const qsizetype step = std::max<qsizetype>(1, d->size() / 2048);
        qsizetype best = 0;
        double bestDist = std::numeric_limits<double>::max();
        for (qsizetype i = 0; i < d->size(); i += step)
        {
            const double dx = std::fabs(d->xAt(i) - data.x());
            if (dx < bestDist)
            {
                bestDist = dx;
                best = i;
            }
        }
        if (x)
            *x = d->xAt(best);
        if (y)
            *y = d->yAt(best);
        return true;
    }
    return false;
}

void Axes::replot()
{
    relayout();
}

Line &Axes::addCurve(Line curve)
{
    m_curves.push_back(curve);
    update();
    return m_curves.last();
}

void Axes::clearCurves()
{
    m_curves.clear();
    update();
}

Bar &Axes::bar(const QStringList &labels, const QVector<double> &values,
               const QVector<QColor> &colors)
{
    auto *b = new Bar(this);          // 父子所有权：Axes 析构时自动 delete
    b->setBars(labels, values, colors);
    b->hide();                        // artist 模式：不作为独立 widget 显示，避免重复绘制
    m_artists.append(b);
    update();
    return *b;
}

Histogram &Axes::hist(const IPlotData *data, int bins)
{
    auto *h = new Histogram(this);
    h->setData(data);
    h->setBins(bins);
    h->hide();                        // artist 模式：关闭 widget 自绘
    m_artists.append(h);
    update();
    return *h;
}

Spectrum &Axes::spectrum(const IPlotData *data, double dt)
{
    auto *s = new Spectrum(this);
    s->setData(data);
    s->setSampleDt(dt);
    s->hide();                        // artist 模式：关闭 widget 自绘
    m_artists.append(s);
    update();
    return *s;
}

void Axes::addArtist(Artist *artist)
{
    if (!artist || m_artists.contains(artist))
        return;
    m_artists.append(artist);
    update();
}

void Axes::clearArtists()
{
    // bar/hist/spectrum 是本 Axes 创建的子对象（parent==this），delete 由 Qt 回收；
    // addArtist 外部挂载的实例不拥有——即便它是 QWidget，也不在此 delete。
    for (Artist *a : m_artists)
    {
        auto *w = dynamic_cast<QWidget *>(a);
        if (w && w->parent() == this)
            delete w;
    }
    m_artists.clear();
    update();
}

void Axes::setAxisLabel(AxisSide side, const QString &text)
{
    axis(side).setLabel(text);
    relayout();
    update();
}

QString Axes::axisLabel(AxisSide side) const
{
    return axis(side).label();
}

bool Axes::autoScaleY(double marginRatio)
{
    double loL = std::numeric_limits<double>::infinity();
    double hiL = -std::numeric_limits<double>::infinity();
    double loR = loL;
    double hiR = hiL;
    bool anyRight = false;

    for (const Line &c : m_curves)
    {
        if (!c.isVisible())
            continue;
        const bool right = (c.m_yAxis == AxisSide::Right) && m_secondAxis;
        anyRight = anyRight || right;
        double &lo = right ? loR : loL;
        double &hi = right ? hiR : hiL;

        if (c.isLod())
        {
            for (const LodPoint &pt : c.m_lod)
            {
                lo = std::min(lo, pt.min);
                hi = std::max(hi, pt.max);
            }
            continue;
        }
        const IPlotData *data = c.m_data;
        if (!data || data->size() <= 0)
            continue;
        const double *px = nullptr;
        const double *py = nullptr;
        const bool direct = data->directAccess(&px, &py);
        for (qsizetype i = 0; i < data->size(); ++i)
        {
            const double y = direct ? py[i] : data->yAt(i);
            lo = std::min(lo, y);
            hi = std::max(hi, y);
        }
    }

    auto apply = [marginRatio](double lo, double hi, double *outLo, double *outHi) {
        if (!std::isfinite(lo) || !std::isfinite(hi))
            return false;
        if (lo == hi)
        {
            const double pad = std::fabs(lo) > 1.0 ? std::fabs(lo) * 0.05 : 1.0;
            *outLo = lo - pad;
            *outHi = hi + pad;
        }
        else
        {
            const double pad = (hi - lo) * marginRatio;
            *outLo = lo - pad;
            *outHi = hi + pad;
        }
        return true;
    };

    bool ok = false;
    double yLo = 0.0;
    double yHi = 0.0;
    if (apply(loL, hiL, &yLo, &yHi))
    {
        setYRange(yLo, yHi);
        ok = true;
    }
    if (anyRight && apply(loR, hiR, &yLo, &yHi))
        setYRange2(yLo, yHi);
    return ok;
}

bool Axes::autoScaleX(double marginRatio)
{
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    for (const Line &c : m_curves)
    {
        if (!c.isVisible())
            continue;
        if (c.isLod())
        {
            for (const LodPoint &pt : c.m_lod)
            {
                lo = std::min(lo, pt.x);
                hi = std::max(hi, pt.x);
            }
            continue;
        }
        const IPlotData *data = c.m_data;
        if (!data || data->size() <= 0)
            continue;
        const double *px = nullptr;
        const double *py = nullptr;
        const bool direct = data->directAccess(&px, &py);
        for (qsizetype i = 0; i < data->size(); ++i)
        {
            const double x = direct ? px[i] : data->xAt(i);
            lo = std::min(lo, x);
            hi = std::max(hi, x);
        }
    }
    if (!std::isfinite(lo) || !std::isfinite(hi))
        return false;
    if (lo == hi)
    {
        const double pad = std::fabs(lo) > 1.0 ? std::fabs(lo) * 0.02 : 1.0;
        lo -= pad;
        hi += pad;
    }
    else
    {
        const double pad = (hi - lo) * marginRatio;
        lo -= pad;
        hi += pad;
    }
    setXRange(lo, hi);
    return true;
}

bool Axes::autoScale()
{
    // 先定 x 再定 y：刻度密度依赖绘图区尺寸，setXRange 会触发 relayout。
    const bool x = autoScaleX();
    const bool y = autoScaleY();
    return x && y;
}

QRectF Axes::dataBounds() const
{
    double xLo = std::numeric_limits<double>::infinity();
    double xHi = -std::numeric_limits<double>::infinity();
    double yLo = xLo, yHi = xHi;
    for (const Line &c : m_curves)
    {
        if (!c.isVisible())
            continue;
        if (c.isLod())
        {
            for (const LodPoint &pt : c.m_lod)
            {
                xLo = std::min(xLo, pt.x);
                xHi = std::max(xHi, pt.x);
                yLo = std::min(yLo, pt.min);
                yHi = std::max(yHi, pt.max);
            }
        }
    }
    if (!std::isfinite(xLo))
        return QRectF();
    return QRectF(QPointF(xLo, yLo), QPointF(xHi, yHi));
}

void Axes::paintCurves(QPainter &painter)
{
    const QRectF plot = m_vp.plotRect();
    const int columns = std::max(1, static_cast<int>(std::lround(plot.width())));

    // 每列一条竖线 + 相邻中点连线：密集数据看起来是一条实心带（正确的视觉语义），
    // 稀疏数据退回普通折线（见下面 data 分支）。
    // y 映射按曲线自己的轴走——双轴就是两套映射，不需要在曲线里做任何特判。
    auto drawEnvelope = [&](const QVector<LodPoint> &pts, AxisSide side) {
        if (pts.isEmpty())
            return;
        QPolygonF mid;
        mid.reserve(pts.size());
        for (const LodPoint &pt : pts)
        {
            const double px = m_vp.xToPixel(pt.x);
            const double pyMin = yToPixel(side, pt.min);
            const double pyMax = yToPixel(side, pt.max);
            painter.drawLine(QPointF(px, pyMin), QPointF(px, pyMax));
            mid.push_back(QPointF(px, (pyMin + pyMax) / 2.0));
        }
        if (mid.size() > 1)
            painter.drawPolyline(mid);
    };

    painter.save();
    painter.setClipRect(plot);
    // 散点符号：brush 填充形状；Cross 只画两条线。
    auto drawMarker = [&](const QPointF &pt, const Line &c) {
        const double r = c.symbolSize() / 2.0;
        if (c.symbol() == Symbol::Cross)
        {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(c.ink(), c.lineWidth()));
            painter.drawLine(pt - QPointF(r, 0), pt + QPointF(r, 0));
            painter.drawLine(pt - QPointF(0, r), pt + QPointF(0, r));
            return;
        }
        painter.setBrush(QBrush(c.ink()));
        painter.setPen(Qt::NoPen);
        switch (c.symbol())
        {
        case Symbol::Circle:
            painter.drawEllipse(pt, r, r);
            break;
        case Symbol::Square:
            painter.drawRect(QRectF(pt.x() - r, pt.y() - r, 2 * r, 2 * r));
            break;
        case Symbol::Diamond:
        {
            QPolygonF d;
            d << pt + QPointF(0, -r) << pt + QPointF(r, 0) << pt + QPointF(0, r)
              << pt + QPointF(-r, 0);
            painter.drawPolygon(d);
            break;
        }
        case Symbol::None:
        case Symbol::Cross:
            break;
        }
    };

    // 一批点的中点像素坐标（LOD 桶或数据点）。
    auto midPoint = [&](const LodPoint &pt, AxisSide side) {
        return QPointF(m_vp.xToPixel(pt.x), yToPixel(side, (pt.min + pt.max) / 2.0));
    };


    for (Line &c : m_curves)
    {
        if (!c.isVisible())
            continue;

        QPen pen(c.m_ink, c.m_lineWidth);
        pen.setCapStyle(Qt::FlatCap);
        painter.setPen(pen);

        if (c.isLod())
        {
            // 内核已分桶：直接画，一行数据都不用扫。
            if (c.scatter())
            {
                for (const LodPoint &pt : c.m_lod)
                    drawMarker(midPoint(pt, c.m_yAxis), c);
            }
            else
            {
                drawEnvelope(c.m_lod, c.m_yAxis);
            }
            continue;
        }

        const IPlotData *data = c.m_data;
        if (!data || data->size() <= 0)
            continue;

        if (data->size() <= columns)
        {
            // 稀疏数据不聚合：逐点折线更真实（聚合会把峰值"抹平"到列内）。
            const double *px = nullptr;
            const double *py = nullptr;
            const bool direct = data->directAccess(&px, &py);
            QPolygonF poly;
            poly.reserve(data->size());
            for (qsizetype i = 0; i < data->size(); ++i)
            {
                const double x = direct ? px[i] : data->xAt(i);
                const double y = direct ? py[i] : data->yAt(i);
                poly.push_back(QPointF(m_vp.xToPixel(x), yToPixel(c.m_yAxis, y)));
            }
            painter.setRenderHint(QPainter::Antialiasing, true);
            if (c.scatter())
            {
                for (const QPointF &pt : poly)
                    drawMarker(pt, c);
            }
            else
            {
                painter.drawPolyline(poly);
            }
            painter.setRenderHint(QPainter::Antialiasing, false);
            continue;
        }

        // 密集数据：像素列 min/max 包络，绘制成本 O(像素宽)。
        const QVector<LodPoint> &pts = c.m_cache.points(*data, m_vp);
        if (c.scatter())
        {
            for (const LodPoint &pt : pts)
                drawMarker(midPoint(pt, c.m_yAxis), c);
        }
        else
        {
            drawEnvelope(pts, c.m_yAxis);
        }
    }

    painter.restore();
}

void Axes::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    relayout();
}

void Axes::relayout()
{
    int left = m_axes[sideIndex(AxisSide::Left)].preferredMargin();
    int right = m_axes[sideIndex(AxisSide::Right)].preferredMargin();
    int top = m_axes[sideIndex(AxisSide::Top)].preferredMargin();
    int bottom = m_axes[sideIndex(AxisSide::Bottom)].preferredMargin();

    // 控件太小时优先保住绘图区，把边距压到最小。
    if (width() - left - right < 40)
    {
        left = std::min(left, 24);
        right = std::min(right, 24);
    }
    if (!m_title.isEmpty())
    {
        // 标题压在最上面：给它一行字高，避免和轴标签抢位置。
        top = std::max(top, int(kTitleBarHeight) + 2);
    }
    // 状态视图行：
    // 每行在顶部预留 band_h + gap，另有行名一行字高（只在有行时生效）。
    if (m_stateBandOverlay.rowCount() > 0)
    {
        constexpr int kStateBandH = 22;
        constexpr int kStateGap = 6;
        top += m_stateBandOverlay.rowCount() * (kStateBandH + kStateGap) + 4;
    }
    if (height() - top - bottom < 30)
    {
        top = std::min(top, 8);
        bottom = std::min(bottom, 16);
    }

    m_vp.setPlotRect(QRectF(left, top,
                            std::max(1, width() - left - right),
                            std::max(1, height() - top - bottom)));
    refreshScaleDivs();
    invalidateStatic();
}

void Axes::refreshScaleDivs()
{
    const QRectF plot = m_vp.plotRect();
    // 刻度密度按像素算：横向 72px 一个主刻度、纵向 48px 一个，避免标签重叠。
    const int xTicks = std::clamp(static_cast<int>(plot.width() / 72.0), 2, 12);
    const int yTicks = std::clamp(static_cast<int>(plot.height() / 48.0), 2, 12);

    const ScaleDiv xDiv = ScaleDiv::linear(m_vp.xLower(), m_vp.xUpper(), xTicks);
    const ScaleDiv yDiv = ScaleDiv::linear(m_vp.yLower(), m_vp.yUpper(), yTicks);

    m_axes[sideIndex(AxisSide::Bottom)].setScaleDiv(xDiv);
    m_axes[sideIndex(AxisSide::Top)].setScaleDiv(xDiv);
    m_axes[sideIndex(AxisSide::Left)].setScaleDiv(yDiv);
    m_axes[sideIndex(AxisSide::Left)].setYRange(m_vp.yLower(), m_vp.yUpper());
    // 右轴用自己的数据范围（双轴各说各话），左轴不替它定界。
    m_axes[sideIndex(AxisSide::Right)].setScaleDiv(
        m_secondAxis ? ScaleDiv::linear(m_y2Lo, m_y2Hi, yTicks) : yDiv);
    m_axes[sideIndex(AxisSide::Right)].setYRange(m_secondAxis ? m_y2Lo : m_vp.yLower(),
                                                 m_secondAxis ? m_y2Hi : m_vp.yUpper());
}

void Axes::markOverlayDirty(quint32 layers)
{
    m_overlayDirtyLayers |= layers;
    // 只有稳态组（StateBand/Marker/Bookmark/AB）内容变化才需要重建缓存；
    // 游标层（Crosshair/CursorReadout）每帧实时画，不影响稳态缓存。
    if (layers & OverlayLayerStableMask)
        m_overlayDirty = true;
}

void Axes::invalidateStatic()
{
    m_staticDirty = true;
    // 稳态叠加层按世界坐标绘制，视口/几何变了像素位置就变，缓存必须一起失效。
    m_overlayDirty = true;
    update();
}

void Axes::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    const QSize cacheSize = size() * devicePixelRatioF();
    if (m_staticDirty || m_staticCache.size() != cacheSize)
    {
        m_staticCache = QPixmap(cacheSize);
        m_staticCache.setDevicePixelRatio(devicePixelRatioF());
        m_staticCache.fill(m_background);

        QPainter p(&m_staticCache);
        p.setRenderHint(QPainter::Antialiasing, false);

        // 网格先画（在曲线之下），轴后画（在网格之上）。
        // 右轴打开时，网格也只画一次——两条轴的网格重合会显得脏。
        for (const Axis &a : m_axes)
            a.paintGrid(&p, m_vp);
        for (const Axis &a : m_axes)
            a.paint(&p, m_vp, rect());

        // 画布边框（老界面「显示」组的 box 开关）：plotRect 四条边。
        if (m_box)
        {
            const QRectF plot = m_vp.plotRect();
            p.setPen(QPen(m_axes[sideIndex(AxisSide::Left)].ink(), 1.0));
            p.setBrush(Qt::NoBrush);
            p.drawRect(plot.adjusted(0.5, 0.5, -0.5, -0.5));
        }

        if (!m_title.isEmpty())
        {
            const QRectF plot = m_vp.plotRect();
            // 标题栏底色：整条横带，与画布内的悬浮工具栏同色（用户要求两者一致）。
            const QColor band = titleBarColor();
            p.setPen(Qt::NoPen);
            p.setBrush(band);
            p.drawRect(QRectF(plot.left(), 1, plot.width(), kTitleBarHeight));
            p.setPen(m_titleColor);
            QFont f = font();
            f.setBold(true);
            p.setFont(f);
            p.drawText(QRectF(plot.left() + 4, 1, plot.width() - 4, kTitleBarHeight - 1),
                       Qt::AlignLeft | Qt::AlignVCenter, m_title);
        }
        p.end();

        m_staticDirty = false;
    }

    QPainter painter(this);
    painter.drawPixmap(0, 0, m_staticCache);
    // 曲线画在静态图层之上；聚合结果按（数据版本, 视口, 列数）缓存，
    // 悬停/重绘不重扫数据。
    paintCurves(painter);
    // artist 图元（bar/hist/spectrum...）画在曲线之上、图例之下。
    // 用绘图区作为像素画布；曲线类 artist 走 paint(axes) 保留坐标映射能力。
    if (!m_artists.isEmpty())
    {
        painter.save();
        painter.setClipRect(m_vp.plotRect());
        for (const Artist *a : m_artists)
        {
            if (!a)
                continue;
            a->paintInRect(painter, m_vp.plotRect());
        }
        painter.restore();
    }
    if (m_legend.isVisible())
        paintLegend(painter);
    paintOverlay(painter);
}

QColor Axes::titleBarColor() const
{
    // 主题里 alternateBase 是最接近"标题/表头带"的中性色，深浅主题都有定义。
    QColor c = palette().alternateBase().color();
    c.setAlpha(255);
    return c;
}

void Axes::paintLegend(QPainter &painter)
{
    // 图例已抽为独立组件 Legend：这里只负责把本帧的曲线/配色/游标读数
    // 组装成 PaintInput 快照交给它画；几何缓存（行/标题/整块矩形）由
    // Legend 自己持有，供 legendHitTest 与读数框避让使用。
    Legend::PaintInput in;
    in.plotRect = m_vp.plotRect();
    in.baseFont = font();
    in.labelColor = m_axes[sideIndex(AxisSide::Left)].labelColor();
    in.windowColor = palette().window().color();
    in.cursorActive = m_cursorReadout.isActive();
    for (int i = 0; i < m_curves.size(); ++i)
    {
        const Line &c = m_curves.at(i);
        if (c.name().isEmpty())
            continue;
        Legend::Item item;
        item.curveIndex = i;
        item.name = c.name();
        item.ink = c.ink();
        item.visible = c.isVisible();
        in.items.append(item);
    }
    if (in.cursorActive)
    {
        in.values.reserve(m_cursorReadout.rows().size());
        for (const ReadoutRow &r : m_cursorReadout.rows())
            in.values.append(Legend::ValueCell{r.value, r.color});
    }
    m_legend.paint(painter, in);
}

int Axes::legendHitTest(const QPointF &pixel) const
{
    return m_legend.hitTest(pixel);
}

void Axes::zoomLegendFont(int steps)
{
    m_legend.zoomFont(steps);
    update();
}

void Axes::toggleLegendItem(int curveIndex)
{
    if (curveIndex < 0 || curveIndex >= m_curves.size())
        return;
    const bool next = !m_curves.at(curveIndex).isVisible();
    m_curves[curveIndex].setVisible(next);
    invalidateStatic();
    update();
    Q_EMIT legendItemToggled(curveIndex, next);
}

void Axes::paintOverlay(QPainter &painter)
{
    // 叠加层调度（第1仗脏标记分层，自研 QPainter 方案，非 PJ4 GPU retained layer 等价物）：
    //   - 稳态组（StateBand/Marker/Bookmark/AB）：内容只在 setXxx 或视口变化时改变，
    //     缓存到透明 QPixmap，游标移动时不重建（P-2 的渲染侧保证）。
    //   - 游标组（Crosshair/CursorReadout/橡皮筋）：随鼠标每帧变化，实时画在稳态层之上。
    // 注意 paintEvent 每帧用 staticCache 覆盖全画布，所以两组都必须每帧"贴/画"一遍——
    // 缓存的是稳态层的"绘制结果"，跳过的是它的重算，而不是跳过把它显示出来。
    OverlayContext ctx;
    ctx.vp = &m_vp;
    ctx.plotRect = m_vp.plotRect();
    ctx.font = font();
    ctx.labelColor = m_axes[sideIndex(AxisSide::Left)].labelColor();
    ctx.axisInk = m_axes[sideIndex(AxisSide::Bottom)].ink();
    ctx.legendBlockRect = m_legend.blockRect();
    ctx.abActive = m_abMeasure.isEnabled();

    // ---- 稳态组：脏时重建透明缓存 ----
    const QSize cacheSize = size() * devicePixelRatioF();
    if (m_overlayDirty || m_overlayCache.size() != cacheSize)
    {
        m_overlayCache = QPixmap(cacheSize);
        m_overlayCache.setDevicePixelRatio(devicePixelRatioF());
        m_overlayCache.fill(Qt::transparent);

        OverlayContext stableCtx = ctx;
        stableCtx.dirtyLayers = m_overlayDirtyLayers;
        QPainter cp(&m_overlayCache);
        // 稳态层内部顺序：色带 → Marker → 书签 → A/B。
        m_stateBandOverlay.paint(cp, stableCtx);
        m_markerOverlay.paint(cp, stableCtx);
        m_bookmarkOverlay.paint(cp, stableCtx);
        m_abMeasure.paint(cp, stableCtx);
        cp.end();

        m_overlayDirty = false;
        m_overlayDirtyLayers = OverlayLayerNone;
        ++m_overlayCacheRebuilds;
    }
    painter.drawPixmap(0, 0, m_overlayCache);

    // ---- 游标组：每帧实时绘制（画在稳态层之上）----
    ctx.dirtyLayers = OverlayLayerCrosshair | OverlayLayerCursorRead;
    m_crosshair.paint(painter, ctx);
    m_cursorReadout.paint(painter, ctx);
    m_crosshair.paintRubberBand(painter, ctx);
}

QImage Axes::grabImage()
{
    QImage image(size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    render(&image);
    return image;
}

} // namespace nextpilot::plotscope::fastplot
