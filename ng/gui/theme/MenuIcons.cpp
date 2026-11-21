#include "MenuIcons.h"

#include "ThemeManager.h"

#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

using namespace nextpilot::plotscope::gui;

namespace
{

// 图形按 16 单位网格设计，实际画在 32px 位图上（2x），交给 Qt 缩到菜单项尺寸，
// 这样 DPI 放大时也不糊。
constexpr int kCanvas = 32;
constexpr qreal kScale = 2.0;

QColor ink()
{
    // 一律走 token：切主题后重画即可，不依赖位图资源。
    return ThemeManager::themeColor(QStringLiteral("text"));
}

void drawBookmark(QPainter &p)
{
    QPainterPath path;
    path.moveTo(4.5, 2.8);
    path.lineTo(11.5, 2.8);
    path.lineTo(11.5, 13.2);
    path.lineTo(8.0, 10.2);
    path.lineTo(4.5, 13.2);
    path.closeSubpath();
    p.drawPath(path);
}

void drawMeasureAB(QPainter &p)
{
    // 两条游标竖线 + 中间双向箭头：读作"量两条游标之间的距离"。
    // （早先画成两竖+顶横，16px 下像一个 H，语义丢了。）
    p.drawLine(QPointF(4.5, 2.8), QPointF(4.5, 13.2));
    p.drawLine(QPointF(11.5, 2.8), QPointF(11.5, 13.2));
    p.drawLine(QPointF(6.2, 8.0), QPointF(9.8, 8.0));
    p.drawLine(QPointF(6.2, 6.6), QPointF(4.7, 8.0));
    p.drawLine(QPointF(6.2, 9.4), QPointF(4.7, 8.0));
    p.drawLine(QPointF(9.8, 6.6), QPointF(11.3, 8.0));
    p.drawLine(QPointF(9.8, 9.4), QPointF(11.3, 8.0));
}

void drawPin(QPainter &p)
{
    p.drawEllipse(QPointF(8.0, 6.4), 2.4, 2.4);
    p.drawLine(QPointF(8.0, 8.8), QPointF(8.0, 12.8));
    p.drawLine(QPointF(5.8, 12.8), QPointF(10.2, 12.8));
}

void drawZoom(QPainter &p, bool plus)
{
    p.drawEllipse(QPointF(6.6, 6.6), 4.0, 4.0);
    p.drawLine(QPointF(9.6, 9.6), QPointF(13.2, 13.2));
    p.drawLine(QPointF(4.3, 6.5), QPointF(8.9, 6.5));
    if (plus)
        p.drawLine(QPointF(6.6, 4.2), QPointF(6.6, 8.8));
}

void drawFit(QPainter &p)
{
    // 四角向内收的框，表示"自适应到可视区"。
    p.drawLine(QPointF(2.6, 6.0), QPointF(2.6, 2.6));
    p.drawLine(QPointF(2.6, 2.6), QPointF(6.0, 2.6));
    p.drawLine(QPointF(10.0, 2.6), QPointF(13.4, 2.6));
    p.drawLine(QPointF(13.4, 2.6), QPointF(13.4, 6.0));
    p.drawLine(QPointF(2.6, 10.0), QPointF(2.6, 13.4));
    p.drawLine(QPointF(2.6, 13.4), QPointF(6.0, 13.4));
    p.drawLine(QPointF(10.0, 13.4), QPointF(13.4, 13.4));
    p.drawLine(QPointF(13.4, 13.4), QPointF(13.4, 10.0));
}

void drawUndo(QPainter &p)
{
    QPainterPath arc;
    arc.arcMoveTo(QRectF(3.0, 4.0, 10.0, 9.0), 200.0);
    arc.arcTo(QRectF(3.0, 4.0, 10.0, 9.0), 200.0, -220.0);
    p.drawPath(arc);
    // 起点的回头箭头（左下）。
    p.drawLine(QPointF(3.0, 8.4), QPointF(6.4, 10.6));
    p.drawLine(QPointF(3.0, 8.4), QPointF(1.9, 11.6));
}

void drawAddCurve(QPainter &p)
{
    QPainterPath line;
    line.moveTo(2.4, 11.4);
    line.lineTo(6.0, 7.0);
    line.lineTo(9.0, 9.4);
    line.lineTo(12.4, 5.0);
    p.drawPath(line);
    // 右下角加号，明确"加一条曲线"而不是"画线"。
    p.drawLine(QPointF(11.4, 12.0), QPointF(14.4, 12.0));
    p.drawLine(QPointF(12.9, 10.5), QPointF(12.9, 13.5));
}

void drawGrid(QPainter &p)
{
    p.drawRect(QRectF(3.0, 3.0, 10.0, 10.0));
    p.drawLine(QPointF(8.0, 3.0), QPointF(8.0, 13.0));
    p.drawLine(QPointF(3.0, 8.0), QPointF(13.0, 8.0));
}

void drawLegend(QPainter &p)
{
    p.drawRect(QRectF(2.8, 4.0, 10.4, 8.0));
    p.drawRect(QRectF(4.2, 5.6, 2.0, 2.0));
    p.drawLine(QPointF(7.4, 6.6), QPointF(11.8, 6.6));
    p.drawRect(QRectF(4.2, 8.8, 2.0, 2.0));
    p.drawLine(QPointF(7.4, 9.8), QPointF(11.8, 9.8));
}

void drawCurve(QPainter &p)
{
    QPainterPath line;
    line.moveTo(2.6, 12.0);
    line.lineTo(6.4, 5.6);
    line.lineTo(9.4, 10.2);
    line.lineTo(13.4, 3.6);
    p.drawPath(line);
}

void drawSplit(QPainter &p, bool horizontal)
{
    // 新增的那一格用半透明填充标出来：光靠一条分隔线，16px 下"向右拆/向下拆"
    // 两个图标几乎一样，用户得靠文案分辨。
    QColor fill = p.pen().color();
    fill.setAlpha(70);
    p.setBrush(fill);
    if (horizontal)
        p.drawRect(QRectF(8.0, 3.0, 5.4, 10.0));
    else
        p.drawRect(QRectF(2.6, 8.0, 10.8, 5.0));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(2.6, 3.0, 10.8, 10.0));
    if (horizontal)
        p.drawLine(QPointF(8.0, 3.0), QPointF(8.0, 13.0));
    else
        p.drawLine(QPointF(2.6, 8.0), QPointF(13.4, 8.0));
}

void drawImage(QPainter &p)
{
    p.drawRect(QRectF(2.8, 3.4, 10.4, 9.2));
    p.drawEllipse(QPointF(10.6, 6.0), 1.2, 1.2);
    QPainterPath hill;
    hill.moveTo(3.6, 11.6);
    hill.lineTo(6.6, 8.2);
    hill.lineTo(9.2, 11.0);
    hill.lineTo(11.0, 9.4);
    hill.lineTo(12.6, 11.6);
    p.drawPath(hill);
}

void drawCsv(QPainter &p)
{
    // 文档形：矩形 + 右上折角太细，16px 下直接画矩形 + 三条"表行"更好认。
    p.drawRect(QRectF(3.6, 2.6, 8.8, 10.8));
    p.drawLine(QPointF(5.4, 5.4), QPointF(10.6, 5.4));
    p.drawLine(QPointF(5.4, 8.0), QPointF(10.6, 8.0));
    p.drawLine(QPointF(5.4, 10.6), QPointF(9.2, 10.6));
}

void drawTrash(QPainter &p)
{
    p.drawLine(QPointF(3.2, 4.4), QPointF(12.8, 4.4));
    p.drawLine(QPointF(6.2, 4.4), QPointF(6.2, 2.8));
    p.drawLine(QPointF(6.2, 2.8), QPointF(9.8, 2.8));
    p.drawLine(QPointF(9.8, 2.8), QPointF(9.8, 4.4));
    QPainterPath body;
    body.moveTo(4.6, 4.4);
    body.lineTo(5.4, 13.4);
    body.lineTo(10.6, 13.4);
    body.lineTo(11.4, 4.4);
    p.drawPath(body);
    p.drawLine(QPointF(6.8, 6.6), QPointF(7.0, 11.4));
    p.drawLine(QPointF(9.2, 6.6), QPointF(9.0, 11.4));
}

void drawEraser(QPainter &p)
{
    // 橡皮：读作"擦掉/清空"，与"移除"（垃圾桶）区分开。
    QPainterPath body;
    body.moveTo(9.4, 3.2);
    body.lineTo(13.0, 6.8);
    body.lineTo(7.6, 12.2);
    body.lineTo(4.0, 8.6);
    body.closeSubpath();
    p.drawPath(body);
    p.drawLine(QPointF(6.6, 5.8), QPointF(10.2, 9.4));
    p.drawLine(QPointF(2.6, 13.2), QPointF(13.4, 13.2));
}

void drawInfo(QPainter &p)
{
    p.drawEllipse(QPointF(8.0, 8.0), 5.4, 5.4);
    p.drawEllipse(QPointF(8.0, 4.9), 0.9, 0.9);
    p.drawLine(QPointF(8.0, 7.0), QPointF(8.0, 11.2));
}

void drawReload(QPainter &p)
{
    QPainterPath arc;
    arc.arcMoveTo(QRectF(3.2, 3.2, 9.6, 9.6), 70.0);
    arc.arcTo(QRectF(3.2, 3.2, 9.6, 9.6), 70.0, -280.0);
    p.drawPath(arc);
    // 顺时针回到起点处的箭头。
    p.drawLine(QPointF(11.6, 3.0), QPointF(9.0, 3.0));
    p.drawLine(QPointF(11.6, 3.0), QPointF(11.6, 5.6));
}

void drawRename(QPainter &p)
{
    p.drawLine(QPointF(4.4, 11.8), QPointF(11.0, 5.2));
    p.drawLine(QPointF(5.6, 12.8), QPointF(12.2, 6.2));
    p.drawLine(QPointF(4.4, 11.8), QPointF(5.6, 12.8));
    p.drawLine(QPointF(3.4, 13.6), QPointF(4.4, 11.8));
}

void drawPause(QPainter &p)
{
    p.fillRect(QRectF(5.2, 3.6, 2.0, 8.8), p.pen().color());
    p.fillRect(QRectF(8.8, 3.6, 2.0, 8.8), p.pen().color());
}

void drawPlay(QPainter &p)
{
    QPainterPath path;
    path.moveTo(5.0, 3.2);
    path.lineTo(12.6, 8.0);
    path.lineTo(5.0, 12.8);
    path.closeSubpath();
    p.fillPath(path, p.pen().color());
}

void drawRemoveAll(QPainter &p)
{
    // 垃圾桶 + 右上角 ×，与"移除数据源"区分开（后者只有一只桶）。
    p.drawLine(QPointF(3.0, 4.6), QPointF(10.4, 4.6));
    QPainterPath body;
    body.moveTo(4.2, 4.6);
    body.lineTo(4.9, 12.8);
    body.lineTo(9.1, 12.8);
    body.lineTo(9.8, 4.6);
    p.drawPath(body);
    p.drawLine(QPointF(11.4, 9.0), QPointF(14.6, 12.2));
    p.drawLine(QPointF(14.6, 9.0), QPointF(11.4, 12.2));
}

bool paint(QPainter &p, const QString &name)
{
    if (name == QLatin1String("bookmark")) { drawBookmark(p); return true; }
    if (name == QLatin1String("measure-ab")) { drawMeasureAB(p); return true; }
    if (name == QLatin1String("pin")) { drawPin(p); return true; }
    if (name == QLatin1String("zoom-in")) { drawZoom(p, true); return true; }
    if (name == QLatin1String("zoom-out")) { drawZoom(p, false); return true; }
    if (name == QLatin1String("fit")) { drawFit(p); return true; }
    if (name == QLatin1String("undo")) { drawUndo(p); return true; }
    if (name == QLatin1String("add-curve")) { drawAddCurve(p); return true; }
    if (name == QLatin1String("grid")) { drawGrid(p); return true; }
    if (name == QLatin1String("legend")) { drawLegend(p); return true; }
    if (name == QLatin1String("curve")) { drawCurve(p); return true; }
    if (name == QLatin1String("split-h")) { drawSplit(p, true); return true; }
    if (name == QLatin1String("split-v")) { drawSplit(p, false); return true; }
    if (name == QLatin1String("image")) { drawImage(p); return true; }
    if (name == QLatin1String("csv")) { drawCsv(p); return true; }
    if (name == QLatin1String("trash")) { drawTrash(p); return true; }
    if (name == QLatin1String("info")) { drawInfo(p); return true; }
    if (name == QLatin1String("reload")) { drawReload(p); return true; }
    if (name == QLatin1String("rename")) { drawRename(p); return true; }
    if (name == QLatin1String("pause")) { drawPause(p); return true; }
    if (name == QLatin1String("play")) { drawPlay(p); return true; }
    if (name == QLatin1String("remove-all")) { drawRemoveAll(p); return true; }
    if (name == QLatin1String("eraser")) { drawEraser(p); return true; }
    return false;
}

} // namespace

QIcon MenuIcons::icon(const QString &name)
{
    // 同一 theme 下结果稳定，缓存避免每次右键都重画十几张位图。
    // key 里带 theme，切主题后自动重画（否则深色主题会拿到浅色字形）。
    static QHash<QString, QIcon> cache;
    const QString key = name + QLatin1Char('@') + QString::number(static_cast<int>(ThemeManager::activeTheme()));
    auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return it.value();

    QPixmap pm(kCanvas, kCanvas);
    pm.fill(Qt::transparent);
    QIcon result;
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.scale(kScale, kScale);
        p.setPen(QPen(ink(), 1.3));
        p.setBrush(Qt::NoBrush);
        if (paint(p, name))
        {
            p.end();
            result = QIcon(pm);
        }
        else
        {
            p.end(); // 未知名字：返回空图标，调用方 setIcon 也不会崩。
        }
    }
    cache.insert(key, result);
    return result;
}
