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
    m_legend = visible;
    if (!visible)
    {
        // 图例关掉后要让读数框恢复"不避让"，否则残留的块矩形会把读数框一直往下推。
        m_legendBlockRect = QRectF();
        m_legendRows.clear();
        m_legendTitleRect = QRectF();
    }
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
    const bool cursorWas = m_cursorActive;
    const QVector<ReadoutRow> abRowsWas = m_abRows;
    m_cursorActive = false;
    m_abRows.clear();
    const bool ok = grabImage().save(path);
    m_cursorActive = cursorWas;
    m_abRows = abRowsWas;
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
    o[QStringLiteral("legend")] = m_legend;
    o[QStringLiteral("legendCollapsed")] = m_legendCollapsed;   // #205
    o[QStringLiteral("legendFontScale")] = m_legendFontScale;   // #205
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
            m_legendFontScale = qBound(0.7, scale, 2.0);
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

void Axes::setRubberBand(const QRect &pixels)
{
    m_rubberBand = pixels;
    update();
}

void Axes::setHover(const QPointF &dataPoint, bool active)
{
    m_hoverPoint = dataPoint;
    m_hoverActive = active;
    update();
}

void Axes::setPlayhead(double x, bool active)
{
    m_playheadX = x;
    m_playheadActive = active;
    update();
}

void Axes::setLinkedCursor(double x, bool active)
{
    m_linkedX = x;
    m_linkedActive = active;
    update();
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
    m_cursorX = x;
    m_cursorActive = active;
    m_cursorPinned = pinned;
    m_cursorHeader = header;
    m_cursorRows = rows;
    update();
}

void Axes::setAB(bool enabled, double a, double b, bool hasB,
                 const QString &header, const QVector<ReadoutRow> &rows,
                 bool drawTriangle, double ay, double by, double ayPixelData)
{
    m_abEnabled = enabled;
    m_abA = a;
    m_abB = b;
    m_abHasB = hasB;
    m_abHeader = header;
    m_abRows = rows;
    m_abTriangle = drawTriangle;
    m_abAy = ay;
    m_abBy = by;
    (void)ayPixelData; // 预留：A 点的 y 像素，供未来精确画直角顶点
    update();
}

void Axes::setBookmarks(const QVector<BookmarkMark> &marks)
{
    m_bookmarks = marks;
    update();
}

void Axes::setMarkers(const QVector<MarkerMark> &marks)
{
    m_markers = marks;
    update();
}

void Axes::setStateBands(const QVector<StateRowMark> &rows)
{
    if (rows.size() == m_stateRows.size() && rows == m_stateRows)
        return; // 内容不变不重排（relayout 有开销）
    m_stateRows = rows;
    relayout(); // 顶部为状态行预留空间（行高 + 行名）
    update();
}

void Axes::setOverlayText(const QString &text)
{
    m_overlayText = text;
    update();
}

void Axes::clearOverlay()
{
    m_hoverActive = false;
    m_overlayText.clear();
    m_rubberBand = QRect();
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
    // 状态视图行（对齐 PJ4 StateTransitionsView 的 RowMetrics：band 22px + gap 6px）：
    // 每行在顶部预留 band_h + gap，另有行名一行字高（只在有行时生效）。
    if (!m_stateRows.isEmpty())
    {
        constexpr int kStateBandH = 22;
        constexpr int kStateGap = 6;
        top += m_stateRows.size() * (kStateBandH + kStateGap) + 4;
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

void Axes::invalidateStatic()
{
    m_staticDirty = true;
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
    if (m_legend)
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
    const QRectF plot = m_vp.plotRect();
    QFont f = font();
    // Ctrl+滚轮字号缩放：因子作用于基准字号（#205）。
    f.setPointSizeF(std::max(6.0, f.pointSizeF() * m_legendFontScale));
    const QFontMetrics fm(f);
    const int rowH = fm.height() + 4;
    painter.save();
    painter.setFont(f);
    const QColor labelColor = m_axes[sideIndex(AxisSide::Left)].labelColor();
    const QColor gray(0x88, 0x88, 0x88);

    // 标题栏：▾/▸ 折叠指示 + "图例"。点击整栏切换折叠。
    //
    // 位置：贴**右上角**。宽度按内容自适应（标题 / 最长曲线名 / 实时值列），
    // 上限绘图区 45%——图例在左上时会横铺一整行，把画面顶部整条吃掉，
    // 而且与读数框抢位置；改成右上角窄块后，绘图区左侧完全让给曲线。
    int legendW = fm.horizontalAdvance(QStringLiteral("▾ 图例")) + 20;
    for (const Line &c : m_curves)
    {
        if (c.name().isEmpty())
            continue;
        int need = 16 + fm.horizontalAdvance(c.name()) + 12;
        if (m_cursorActive)
            need += 12 + 70;   // 实时值列
        legendW = std::max(legendW, need);
    }
    legendW = std::max(96, std::min(legendW, int(plot.width() * 0.45)));
    const double legendX = plot.right() - 8 - legendW;

    // 行数（名字为空的曲线不进图例）。要先把整块底色铺上，否则曲线会从字里穿过去，
    // 右上角窄块正好压在波形上时尤其难看。
    int namedRows = 0;
    for (const Line &c : m_curves)
        if (!c.name().isEmpty())
            ++namedRows;

    const QRectF blockRect(legendX, plot.top() + 8 + m_legendTopInset, legendW,
                           (namedRows + 1) * rowH);
    QColor blockBg = palette().window().color();
    blockBg.setAlpha(232);
    painter.setPen(QPen(QColor(labelColor.red(), labelColor.green(), labelColor.blue(), 60), 1));
    painter.setBrush(blockBg);
    painter.drawRoundedRect(blockRect, 3, 3);

    m_legendTitleRect = QRectF(legendX, plot.top() + 8 + m_legendTopInset, legendW, rowH);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 36));
    painter.drawRoundedRect(m_legendTitleRect, 2, 2);
    painter.setPen(labelColor);
    painter.drawText(QRectF(m_legendTitleRect.left() + 4, m_legendTitleRect.top(), 80, rowH),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     m_legendCollapsed ? QStringLiteral("▸ 图例") : QStringLiteral("▾ 图例"));

    m_legendRows.clear();
    if (m_legendCollapsed)
    {
        m_legendBlockRect = m_legendTitleRect;
        painter.restore();
        return;
    }

    // 每条命名曲线一行：可见用曲线色，隐藏灰显（PJ4 语义：隐藏行保留可点击切回）。
    int row = 0;
    for (int i = 0; i < m_curves.size(); ++i)
    {
        const Line &c = m_curves.at(i);
        if (c.name().isEmpty())
            continue;
        const QPointF origin(legendX, plot.top() + 8 + m_legendTopInset + (row + 1) * rowH);
        painter.setPen(Qt::NoPen);
        painter.setBrush(c.isVisible() ? c.ink() : gray);
        painter.drawRect(QRectF(origin.x(), origin.y() + 3, 10, 10));
        painter.setPen(c.isVisible() ? labelColor : gray);
        painter.drawText(QPointF(origin.x() + 16, origin.y() + fm.ascent() + 1), c.name());
        // PJ4 图例实时值：游标激活时，图例每条曲线名后显示该时刻数值（与读数框同源、同行序）。
        if (m_cursorActive && row < m_cursorRows.size())
        {
            const QString val = m_cursorRows.at(row).value;
            painter.setPen(m_cursorRows.at(row).color);
            painter.drawText(QRectF(origin.x() + 16 + qMin(150, fm.horizontalAdvance(c.name()) + 12),
                                    origin.y(), 70, rowH),
                             Qt::AlignVCenter | Qt::AlignLeft, val);
        }
        m_legendRows.append(LegendRow{i, QRectF(legendX, origin.y(), legendW, rowH)});
        ++row;
    }
    // 整块矩形：标题栏一行 + 所有曲线行（读数框按它避让）。
    m_legendBlockRect = blockRect;
    painter.restore();
}

int Axes::legendHitTest(const QPointF &pixel) const
{
    if (!m_legend)
        return -2;
    if (m_legendTitleRect.contains(pixel))
        return -1;
    for (const LegendRow &r : m_legendRows)
        if (r.rect.contains(pixel))
            return r.curveIndex;
    return -2;
}

void Axes::setLegendCollapsed(bool collapsed)
{
    if (m_legendCollapsed == collapsed)
        return;
    m_legendCollapsed = collapsed;
    update();
    Q_EMIT legendCollapseChanged(collapsed);
}

void Axes::zoomLegendFont(int steps)
{
    const double next = qBound(0.7, m_legendFontScale + steps * 0.1, 2.0);
    if (next == m_legendFontScale)
        return;
    m_legendFontScale = next;
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
    const QRectF plot = m_vp.plotRect();

    if (!m_overlayText.isEmpty())
    {
        painter.save();
        QFont f = font();
        f.setPointSizeF(std::max(7.0, f.pointSizeF()));
        painter.setFont(f);
        const QFontMetrics fm(f);
        const int pad = 6;
        const QRectF textRect(plot.right() - fm.horizontalAdvance(m_overlayText) - 2 * pad,
                              plot.top() + pad,
                              fm.horizontalAdvance(m_overlayText) + 2 * pad,
                              fm.height() + pad);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 40));
        painter.drawRoundedRect(textRect, 3, 3);
        painter.setPen(m_axes[sideIndex(AxisSide::Left)].labelColor());
        painter.drawText(textRect, Qt::AlignCenter, m_overlayText);
        painter.restore();
    }

    if (m_hoverActive && m_vp.plotRect().contains(m_vp.toPixel(m_hoverPoint.x(), m_hoverPoint.y())))
    {
        const QPointF px = m_vp.toPixel(m_hoverPoint.x(), m_hoverPoint.y());
        painter.save();
        painter.setClipRect(plot);
        QPen pen(QColor(0x88, 0x88, 0x88, 0xC0), 1.0, Qt::DashLine);
        painter.setPen(pen);
        painter.drawLine(QPointF(px.x(), plot.top()), QPointF(px.x(), plot.bottom()));
        painter.drawLine(QPointF(plot.left(), px.y()), QPointF(plot.right(), px.y()));
        painter.setBrush(QColor(0x1F, 0x6F, 0xC0));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(px, 3.0, 3.0);
        painter.restore();
    }

    // 播放头竖线（对齐 PJ4 tracker needle）：跨整幅绘图区的醒目橙红竖线。
    // 用数据 x 映射像素，落在可见范围外则不画（随播放推进自然进/出视野）。
    if (m_playheadActive)
    {
        const QPointF px = m_vp.toPixel(m_playheadX, m_vp.yLower());
        if (px.x() >= plot.left() && px.x() <= plot.right())
        {
            painter.save();
            painter.setClipRect(plot);
            painter.setPen(QPen(QColor(QStringLiteral("#f2b134")), 1.6, Qt::SolidLine));
            painter.drawLine(QPointF(px.x(), plot.top()), QPointF(px.x(), plot.bottom()));
            // 顶部小三角，便于在密集曲线中定位。
            QPolygonF head;
            head << QPointF(px.x() - 4, plot.top()) << QPointF(px.x() + 4, plot.top())
                 << QPointF(px.x(), plot.top() + 6);
            painter.setBrush(QColor(QStringLiteral("#f2b134")));
            painter.setPen(Qt::NoPen);
            painter.drawPolygon(head);
            painter.restore();
        }
    }

    // 联动游标竖线（同页其它画布同步的时间，只画竖线，对齐老 setLinkedCursor）。
    if (m_linkedActive)
    {
        const double lx = m_vp.toPixel(m_linkedX, m_vp.yLower()).x();
        if (lx >= plot.left() && lx <= plot.right())
        {
            painter.save();
            painter.setClipRect(plot);
            painter.setPen(QPen(QColor(0x5b, 0xc0, 0xf0, 0xB0), 1.0, Qt::DashLine));
            painter.drawLine(QPointF(lx, plot.top()), QPointF(lx, plot.bottom()));
            painter.restore();
        }
    }

    // 读数叠加层：状态色带（顶部预留区）→ Marker 规则产出 → 书签 → A/B → 十字游标读数
    // （后者画在最上，读数框不被遮挡）。
    paintStateBands(painter, plot);
    paintMarkers(painter, plot);
    paintBookmarks(painter, plot);
    paintAB(painter, plot);
    paintCursorReadout(painter, plot);

    if (!m_rubberBand.isNull())
    {
        painter.save();
        painter.setPen(QPen(m_axes[sideIndex(AxisSide::Bottom)].ink(), 1.0));
        painter.setBrush(QColor(0x1F, 0x6F, 0xC0, 40));
        painter.drawRect(m_rubberBand);
        painter.restore();
    }
}

// 十字游标 + 逐曲线读数框（对齐老 PlottingCanvas::paintEvent 的读数块）。
void Axes::paintCursorReadout(QPainter &painter, const QRectF &plot)
{
    if (!m_cursorActive)
        return;
    const double x = m_vp.toPixel(m_cursorX, m_vp.yLower()).x();
    if (x < plot.left() || x > plot.right())
        return;

    painter.save();
    painter.setClipRect(plot);
    QFont f = font();
    f.setPointSizeF(std::max(7.0, f.pointSizeF()));
    painter.setFont(f);
    const QFontMetrics fm(f);

    // 竖线：固定读数用实线 accent，普通悬停用虚线灰。
    const QColor accent(QStringLiteral("#f2b134"));
    painter.setPen(m_cursorPinned ? QPen(accent, 1.2, Qt::SolidLine)
                                  : QPen(QColor(0x88, 0x88, 0x88, 0xC0), 1.0, Qt::DashLine));
    painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    painter.setBrush(m_cursorPinned ? accent : QColor(0x1F, 0x6F, 0xC0));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPointF(x, plot.top() + 1), 3.0, 3.0);

    // 读数框：贴游标右侧，越界翻到左侧；最多 8 行。
    const int lineH = fm.height() + 2;
    const int boxW = qMin(int(plot.width() - 16), 280);
    const int boxH = 22 + qMin(m_cursorRows.size(), 8) * lineH + 6;
    // 读数框避开图例：图例现在是**右上角**的自适应窄块，不再横铺一整行，
    // 所以不能用"游标靠左就整体下移"的老规则（那会把框白推下去一大截）。
    // 改为几何判定：按默认位置算出框，只有真正与图例矩形相交时才让开。
    double boxTop = plot.top() + (m_abEnabled ? 30 : 8);
    QRectF box(x + 12, boxTop, boxW, boxH);
    if (box.right() > plot.right())
        box.moveLeft(x - 12 - boxW);
    if (box.bottom() > plot.bottom())
        box.moveTop(plot.bottom() - boxH - 4);
    // 真正压到右上角图例时，把框整体挪到图例下方（图例窄，绝大多数游标位置都不触发）。
    if (!m_legendBlockRect.isEmpty() && box.intersects(m_legendBlockRect))
        box.moveTop(m_legendBlockRect.bottom() + 4);
    if (box.bottom() > plot.bottom())
        box.moveTop(plot.bottom() - boxH - 4);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x14, 0x1a, 0x20, m_cursorPinned ? 252 : 238));
    painter.drawRoundedRect(box, 3, 3);
    if (m_cursorPinned)
    {
        painter.setPen(QPen(accent, 1));
        painter.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    }

    painter.setPen(accent);
    painter.drawText(QRectF(box.left() + 8, box.top() + 3, box.width() - 16, lineH),
                     Qt::AlignVCenter | Qt::AlignLeft, m_cursorHeader);
    painter.setPen(QColor(0x7a, 0x84, 0x8f));
    painter.drawLine(QPointF(box.left() + 7, box.top() + 21),
                     QPointF(box.right() - 7, box.top() + 21));

    int y = int(box.top()) + 23;
    const QColor textColor = m_axes[sideIndex(AxisSide::Left)].labelColor();
    for (int i = 0; i < m_cursorRows.size() && i < 8; ++i)
    {
        const ReadoutRow &r = m_cursorRows.at(i);
        painter.setPen(r.color);
        painter.drawText(QRectF(box.left() + 8, y, box.width() * 0.55, lineH),
                         Qt::AlignVCenter | Qt::AlignLeft,
                         fm.elidedText(r.label, Qt::ElideMiddle, int(box.width() * 0.55)));
        painter.setPen(textColor);
        painter.drawText(QRectF(box.left() + box.width() * 0.55, y, box.width() * 0.4, lineH),
                         Qt::AlignVCenter | Qt::AlignRight, r.value);
        y += lineH;
    }
    painter.restore();
}

// A/B 双游标测量（对齐老 PlottingCanvas：区间带 + ΔX 徽标 + A/B 直角三角 + 逐曲线 Δy/k）。
void Axes::paintAB(QPainter &painter, const QRectF &plot)
{
    if (!m_abEnabled)
        return;
    const QColor accent(QStringLiteral("#f2b134"));
    const QColor bColor(QStringLiteral("#5bc0f0"));
    painter.save();
    painter.setClipRect(plot);
    QFont f = font();
    f.setPointSizeF(std::max(7.0, f.pointSizeF()));
    painter.setFont(f);
    const QFontMetrics fm(f);

    const double xa = m_vp.toPixel(m_abA, m_vp.yLower()).x();
    if (xa < plot.left() || xa > plot.right())
    {
        painter.restore();
        return;
    }
    if (!m_abHasB)
    {
        const QString hint = QStringLiteral("点击图表设置游标 A，再点一下设置 B");
        const QRectF hr(plot.left() + 10, plot.top() + 4, fm.horizontalAdvance(hint) + 20, 20);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0x14, 0x1a, 0x20, 220));
        painter.drawRoundedRect(hr, 3, 3);
        painter.setPen(accent);
        painter.drawText(hr.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, hint);
        painter.restore();
        return;
    }

    const double xb = m_vp.toPixel(m_abB, m_vp.yLower()).x();
    // 区间带（可能 A 在 B 右侧）。
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0xf2, 0xb1, 0x34, 24));
    painter.drawRect(QRectF(QPointF(qMin(xa, xb), plot.top()),
                            QPointF(qMax(xa, xb), plot.bottom())));

    // A / B 竖线 + 顶部圆点。
    painter.setPen(QPen(accent, 1.4, Qt::DashLine));
    painter.drawLine(QPointF(xa, plot.top()), QPointF(xa, plot.bottom()));
    painter.setBrush(accent);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPointF(xa, plot.top() + 1), 3, 3);
    if (xb >= plot.left() && xb <= plot.right())
    {
        painter.setPen(QPen(bColor, 1.4, Qt::DashLine));
        painter.drawLine(QPointF(xb, plot.top()), QPointF(xb, plot.bottom()));
        painter.setBrush(bColor);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(xb, plot.top() + 1), 3, 3);
    }

    const QColor textColor = m_axes[sideIndex(AxisSide::Left)].labelColor();
    // ΔX 徽标（居中于 A/B，夹在绘图区内）。
    const int tw = fm.horizontalAdvance(m_abHeader) + 20;
    QRectF badge((xa + xb) * 0.5 - tw * 0.5, plot.top() + 5, tw, 20);
    if (badge.left() < plot.left() + 4) badge.moveLeft(plot.left() + 4);
    if (badge.right() > plot.right() - 4) badge.moveRight(plot.right() - 4);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0xf2, 0xb1, 0x34, 235));
    painter.drawRoundedRect(badge, 3, 3);
    painter.setPen(QColor(0x10, 0x16, 0x1c));
    painter.drawText(badge, Qt::AlignCenter, m_abHeader);

    // 直角三角形：首条可见曲线的 Δy 一眼可见（y 值由上层换算成像素 y 传入）。
    if (m_abTriangle && m_abAy >= plot.top() && m_abAy <= plot.bottom())
    {
        painter.setPen(QPen(QColor(0xf2, 0xb1, 0x34, 150), 1, Qt::DashLine));
        painter.drawLine(QPointF(xa, m_abAy), QPointF(xb, m_abAy));
        painter.drawLine(QPointF(xb, m_abAy), QPointF(xb, m_abBy));
        painter.drawLine(QPointF(xa, m_abAy), QPointF(xb, m_abBy));
    }

    // 逐曲线 Δy / k 读数框（贴底部）。
    if (!m_abRows.isEmpty())
    {
        const int lineH = fm.height() + 2;
        const int boxH = 40 + qMin(m_abRows.size(), 6) * lineH + 6;
        QRectF box(plot.left() + 10, plot.bottom() - boxH - 8, qMin(int(plot.width() - 20), 340), boxH);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0x14, 0x1a, 0x20, 238));
        painter.drawRoundedRect(box, 3, 3);
        painter.setPen(accent);
        painter.drawText(QRectF(box.left() + 8, box.top() + 3, box.width() - 16, lineH),
                         Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("A→B  Δx / 各曲线 Δy 与斜率"));
        painter.setPen(QColor(0x7a, 0x84, 0x8f));
        painter.drawLine(QPointF(box.left() + 7, box.top() + 21), QPointF(box.right() - 7, box.top() + 21));
        int y = int(box.top()) + 23;
        for (int i = 0; i < m_abRows.size() && i < 6; ++i)
        {
            const ReadoutRow &r = m_abRows.at(i);
            painter.setPen(r.color);
            painter.drawText(QRectF(box.left() + 8, y, box.width() * 0.45, lineH),
                             Qt::AlignVCenter | Qt::AlignLeft,
                             fm.elidedText(r.label, Qt::ElideMiddle, int(box.width() * 0.45)));
            painter.setPen(textColor);
            painter.drawText(QRectF(box.left() + box.width() * 0.45, y, box.width() * 0.53, lineH),
                             Qt::AlignVCenter | Qt::AlignRight, r.value);
            y += lineH;
        }
    }
    painter.restore();
}

// 时间书签（对齐老 PlottingCanvas：竖线 + 顶部 chip + 视窗外边缘提示）。
void Axes::paintBookmarks(QPainter &painter, const QRectF &plot)
{
    if (m_bookmarks.isEmpty())
        return;
    painter.save();
    painter.setClipRect(plot);
    QFont f = font();
    f.setPointSizeF(std::max(7.0, f.pointSizeF()));
    painter.setFont(f);
    const QFontMetrics fm(f);
    const QColor accent(QStringLiteral("#f2b134"));
    int lane = 0;
    for (const BookmarkMark &mk : m_bookmarks)
    {
        const double x = m_vp.toPixel(mk.t, m_vp.yLower()).x();
        if (x < plot.left() || x > plot.right())
            continue;
        painter.setPen(QPen(QColor(0xf2, 0xb1, 0x34, 150), 1, Qt::DashLine));
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        const QString text = fm.elidedText(mk.label, Qt::ElideRight, 180);
        const int tw = fm.horizontalAdvance(text) + 10;
        QRectF chip(x + 3, plot.top() + 2 + (lane % 3) * 17, tw, 16);
        if (chip.right() > plot.right()) chip.moveLeft(x - 3 - tw);
        if (chip.left() < plot.left()) chip.moveLeft(plot.left() + 3);
        painter.setPen(QPen(mk.selected ? accent : QColor(0x4a, 0x52, 0x5c), mk.selected ? 2 : 1));
        painter.setBrush(QColor(0x14, 0x1a, 0x20, mk.selected ? 255 : 235));
        painter.drawRoundedRect(chip, 2, 2);
        painter.setPen(accent);
        painter.drawText(chip.adjusted(5, 0, -5, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
        ++lane;
    }
    painter.restore();
}

// Marker 规则产出：事件竖线/点、时间区间带、值区间带（对齐 PJ4 PlotMarkersItem）。
void Axes::paintMarkers(QPainter &painter, const QRectF &plot)
{
    if (m_markers.isEmpty())
        return;
    painter.save();
    painter.setClipRect(plot);
    QFont f = font();
    f.setPointSizeF(std::max(7.0, f.pointSizeF()));
    painter.setFont(f);
    const QFontMetrics fm(f);
    const auto sevColor = [](MarkerMark::Severity s) -> QColor {
        switch (s)
        {
        case MarkerMark::Severity::Warning:  return QColor(0xf2, 0xb1, 0x34);
        case MarkerMark::Severity::Error:    return QColor(0xe5, 0x4c, 0x3a);
        case MarkerMark::Severity::Critical: return QColor(0xd1, 0x2b, 0x2b);
        case MarkerMark::Severity::Info:
        default:                             return QColor(0x3d, 0x8f, 0xd6);
        }
    };
    int lane = 0;
    for (const MarkerMark &mk : m_markers)
    {
        const QColor col = mk.color.isValid() ? mk.color : sevColor(mk.severity);
        if (mk.kind == MarkerMark::Kind::ValueBand)
        {
            // 值带：横跨绘图区的半透明水平带 + 左端 label。
            const double yp1 = yToPixel(AxisSide::Left, mk.valueLow);
            const double yp2 = yToPixel(AxisSide::Left, mk.valueHigh);
            const double top = std::min(yp1, yp2), bottom = std::max(yp1, yp2);
            if (bottom < plot.top() || top > plot.bottom())
                continue;
            const QRectF band(plot.left(), top, plot.width(), std::max(2.0, bottom - top));
            painter.fillRect(band, QColor(col.red(), col.green(), col.blue(), 30));
            painter.setPen(QPen(col, 1, Qt::DashLine));
            painter.drawLine(QPointF(plot.left(), top), QPointF(plot.right(), top));
            painter.drawLine(QPointF(plot.left(), bottom), QPointF(plot.right(), bottom));
            if (!mk.label.isEmpty())
            {
                painter.setPen(col);
                painter.drawText(QPointF(plot.left() + 4, std::max(plot.top() + 11.0, top - 3.0)),
                                 fm.elidedText(mk.label, Qt::ElideRight, 160));
            }
            continue;
        }

        const double x = m_vp.toPixel(mk.tStart, m_vp.yLower()).x();
        if (x < plot.left() || x > plot.right())
            continue;
        if (mk.kind == MarkerMark::Kind::Region)
        {
            const double x2 = m_vp.toPixel(mk.tEnd, m_vp.yLower()).x();
            const double left = std::max(plot.left(), x), right = std::min(plot.right(), x2);
            painter.fillRect(QRectF(left, plot.top(), std::max(0.0, right - left), plot.height()),
                             QColor(col.red(), col.green(), col.blue(), 24));
            painter.setPen(QPen(col, 1, Qt::DashLine));
            painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            painter.drawLine(QPointF(x2, plot.top()), QPointF(x2, plot.bottom()));
        }
        else // Event：竖线（hasValue 时在值处画点）
        {
            painter.setPen(QPen(col, mk.hasValue ? 2 : 1, Qt::SolidLine));
            painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            if (mk.hasValue)
            {
                const double yp = yToPixel(AxisSide::Left, mk.valueLow);
                painter.setPen(Qt::NoPen);
                painter.setBrush(col);
                painter.drawEllipse(QPointF(x, yp), 4.0, 4.0);
            }
        }
        if (!mk.label.isEmpty())
        {
            QRectF chip(x + 3, plot.top() + 2 + (lane % 3) * 15,
                        fm.horizontalAdvance(mk.label) + 8, 14);
            if (chip.right() > plot.right()) chip.moveLeft(x - 3 - chip.width());
            if (chip.left() < plot.left()) chip.moveLeft(plot.left() + 2);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0x14, 0x1a, 0x20, 220));
            painter.drawRoundedRect(chip, 2, 2);
            painter.setPen(col);
            painter.drawText(chip.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                             fm.elidedText(mk.label, Qt::ElideRight, 160));
        }
        ++lane;
    }
    painter.restore();
}

// 状态视图色带（对齐 PJ4 StateTransitionsView）：
// 每行画在绘图区上方的预留带里（band 22px + gap 6px），不遮挡曲线；
// 段 = 半透明填充 + 1px 边框，宽度够时把状态名画在段内（居中、超出省略）；
// 行名画在段带左侧（精简版 PJ4 行名 chip：名与 band 同行，左对齐固定宽）。
void Axes::paintStateBands(QPainter &painter, const QRectF &plot)
{
    if (m_stateRows.isEmpty())
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    QFont f = font();
    f.setPointSizeF(std::max(7.0, f.pointSizeF()));
    painter.setFont(f);
    const QFontMetrics fm(f);
    constexpr double kBandH = 22.0;
    constexpr double kGap = 6.0;
    constexpr double kMinLabelPx = 24.0; // 段宽低于此不画状态名（对齐 PJ4 kBarMinLabelWidthPx 精神）
    constexpr double kNameW = 90.0;      // 行名固定宽（超出省略）
    const double bandLeft = plot.left();

    double y = plot.top() - m_stateRows.size() * (kBandH + kGap) - 4.0;
    for (const StateRowMark &row : m_stateRows)
    {
        const double bandY = y;
        const double segLeft = bandLeft + kNameW + 4.0;
        // 行名画在带内左侧。
        painter.setPen(m_axes[sideIndex(AxisSide::Bottom)].ink());
        painter.drawText(QRectF(bandLeft, bandY, kNameW, kBandH),
                         Qt::AlignVCenter | Qt::AlignLeft,
                         fm.elidedText(row.channel, Qt::ElideMiddle, static_cast<int>(kNameW)));

        for (const StateBandMark &seg : row.segments)
        {
            const double x0 = m_vp.toPixel(seg.tStart, m_vp.yLower()).x();
            const double x1 = m_vp.toPixel(seg.tEnd, m_vp.yLower()).x();
            const double left = std::max(segLeft, x0);
            const double right = std::min(plot.right(), x1);
            if (right - left < 1.0)
                continue; // 视口外或被行名遮住
            const QColor col = seg.color.isValid() ? seg.color : stateColor(seg.label);
            const QRectF rect(left, bandY, right - left, kBandH);
            QColor fill = col;
            fill.setAlphaF(0.82); // 中饱和：段内黑/白字都可读（对齐 PJ4 语义）
            painter.fillRect(rect, fill);
            painter.setPen(QPen(QColor(0, 0, 0, 90), 1.0));
            painter.drawRect(rect.adjusted(0, 0, -1, -1));
            if (rect.width() > kMinLabelPx && !seg.label.isEmpty())
            {
                // 按背景亮度选黑/白字。
                const int brightness = (col.red() * 299 + col.green() * 587 + col.blue() * 114) / 1000;
                painter.setPen(brightness > 140 ? Qt::black : Qt::white);
                painter.drawText(rect, Qt::AlignCenter,
                                 fm.elidedText(seg.label, Qt::ElideRight,
                                               static_cast<int>(rect.width() - 4)));
            }
        }
        y += kBandH + kGap;
    }
    painter.restore();
}

QImage Axes::grabImage()
{
    QImage image(size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    render(&image);
    return image;
}

} // namespace nextpilot::plotscope::fastplot
