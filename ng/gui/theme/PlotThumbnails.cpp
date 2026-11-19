#include "PlotThumbnails.h"
#include "ThemeManager.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <cmath>

using namespace ng::gui;

namespace
{
// 抄自老 PlottingRibbon::plotThumb：颜色经主题适配，深浅主题都可读。
QColor ink(const QColor &c)
{
    return ThemeManager::adaptColor(c, ThemeManager::activeTheme());
}
QColor panelBg()
{
    // ng 的 token 是 "panel"（老工程叫 panelAlt，这里别照抄老 token 名——
    // ThemeManager 对未知 token 返回醒目洋红 #FF00FF，正是之前缩略图发紫的根因）。
    return ThemeManager::themeColor(QStringLiteral("panel"));
}
} // namespace

QIcon PlotThumbnails::plot(const QString &kind)
{
    QPixmap pm(96, 56);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    // 文件格式/实时源：画一张"文件卡"——左色块 + 类型缩写，比纯文字好认。
    static const struct
    {
        const char *key;
        const char *tag;
        QColor color;
    } kFormats[] = {
        {"csv", "CSV", QColor("#4ea1ff")},
        {"ulog", "ULOG", QColor("#ffb454")},
        {"custom-protocol", "PROTO", QColor("#d2a8ff")},
        {"mavlink", "MAV", QColor("#7ee787")},
        {"nmea", "NMEA", QColor("#39c5cf")},
        {"stream", "LIVE", QColor("#ff7b72")},
    };
    for (const auto &f : kFormats)
    {
        if (kind != QLatin1String(f.key))
            continue;
        p.setPen(QPen(ink(QColor("#3a4650")), 1));
        p.setBrush(panelBg());
        p.drawRoundedRect(QRectF(0.5, 0.5, 95, 54.5), 3, 3);
        p.setPen(Qt::NoPen);
        p.setBrush(ink(f.color));
        p.drawRoundedRect(QRectF(6, 10, 30, 36), 2, 2);
        p.setPen(ink(QColor("#c9d1d9")));
        QFont fo = p.font();
        fo.setPixelSize(11);
        fo.setBold(true);
        p.setFont(fo);
        p.drawText(QRectF(40, 10, 50, 36), Qt::AlignVCenter, QLatin1String(f.tag));
        return QIcon(pm);
    }

    // 图型缩略图：统一的坐标框。
    p.setPen(QPen(ink(QColor("#2d3841")), 1));
    p.setBrush(panelBg());
    p.drawRoundedRect(QRectF(0.5, 0.5, 95, 55), 3, 3);
    const QRectF r(9, 9, 78, 38);
    p.setPen(QPen(ink(QColor("#26313a")), 1));
    p.drawLine(r.bottomLeft(), r.bottomRight());
    p.drawLine(r.bottomLeft(), r.topLeft());

    if (kind == QLatin1String("timeseries"))
    {
        QPainterPath path;
        for (int i = 0; i <= 40; ++i)
        {
            const double x = r.left() + r.width() * i / 40.0;
            const double y = r.center().y() - std::sin(i * 0.28) * 8.0;
            if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
        }
        p.setPen(QPen(ink(QColor("#4ea1ff")), 1.6));
        p.drawPath(path);
    }
    else if (kind == QLatin1String("xy"))
    {
        for (int i = 0; i < 9; ++i)
        {
            const double x = r.left() + 6 + (i * 13) % 50;
            const double y = r.bottom() - 3 - (i * 7) % 20;
            p.setPen(QPen(ink(QColor("#ffb454")), 1));
            p.setBrush(ink(QColor("#ffb454")));
            p.drawEllipse(QPointF(x, y), 1.8, 1.8);
        }
    }
    else if (kind == QLatin1String("scatter"))
    {
        for (int i = 0; i < 14; ++i)
        {
            const double x = r.left() + 5 + (i * 17) % 52;
            const double y = r.bottom() - 2 - (i * 11) % 21;
            p.setPen(Qt::NoPen);
            p.setBrush(ink(i % 3 ? QColor("#7ee787") : QColor("#ffb454")));
            p.drawEllipse(QPointF(x, y), 1.5, 1.5);
        }
    }
    else if (kind == QLatin1String("map"))
    {
        for (int i = 1; i < 4; ++i)
        {
            p.drawLine(QPointF(r.left() + r.width() * i / 4.0, r.top()),
                       QPointF(r.left() + r.width() * i / 4.0, r.bottom()));
            p.drawLine(QPointF(r.left(), r.top() + r.height() * i / 4.0),
                       QPointF(r.right(), r.top() + r.height() * i / 4.0));
        }
        QPainterPath path;
        path.moveTo(r.left() + 6, r.bottom() - 4);
        path.cubicTo(r.left() + 20, r.top() + 2, r.left() + 34, r.bottom() - 6, r.right() - 4, r.top() + 6);
        p.setPen(QPen(ink(QColor("#4ea1ff")), 1.5));
        p.drawPath(path);
        p.setBrush(ink(QColor("#ff7b72")));
        p.drawEllipse(QPointF(r.right() - 4, r.top() + 6), 2.2, 2.2);
    }
    else if (kind == QLatin1String("bar"))
    {
        for (int i = 0; i < 5; ++i)
        {
            const double h = 4 + (i * 7) % 19;
            p.setPen(Qt::NoPen);
            p.setBrush(ink(QColor("#7ee787")));
            p.drawRect(QRectF(r.left() + 5 + i * 11, r.bottom() - h, 6, h));
        }
    }
    else if (kind == QLatin1String("histogram"))
    {
        for (int i = 0; i < 7; ++i)
        {
            const double h = 3 + (i * 5 + i * i) % 21;
            p.setPen(Qt::NoPen);
            p.setBrush(ink(QColor("#d2a8ff")));
            p.drawRect(QRectF(r.left() + 4 + i * 8, r.bottom() - h, 6, h));
        }
    }
    else if (kind == QLatin1String("fft"))
    {
        QPainterPath path;
        for (int i = 0; i <= 32; ++i)
        {
            const double x = r.left() + r.width() * i / 32.0;
            const double env = std::exp(-i * 0.12);
            const double y = r.bottom() - 2 - std::fabs(std::sin(i * 0.55)) * 18.0 * env;
            if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
        }
        p.setPen(QPen(ink(QColor("#39c5cf")), 1.4));
        p.drawPath(path);
    }
    else if (kind == QLatin1String("traj3d"))
    {
        p.setPen(QPen(ink(QColor("#26313a")), 1));
        p.drawLine(QPointF(12, 28), QPointF(32, 21));
        p.drawLine(QPointF(32, 21), QPointF(58, 27));
        p.drawLine(QPointF(58, 27), QPointF(58, 9));
        p.drawLine(QPointF(58, 9), QPointF(36, 8));
        p.drawLine(QPointF(36, 8), QPointF(12, 14));
        p.drawLine(QPointF(12, 14), QPointF(12, 28));
        QPainterPath path;
        path.moveTo(14, 25);
        path.cubicTo(24, 8, 40, 12, 54, 20);
        p.setPen(QPen(ink(QColor("#f2b134")), 1.6));
        p.drawPath(path);
    }
    else if (kind == QLatin1String("surface3d"))
    {
        p.setPen(QPen(ink(QColor("#2a3b48")), 1));
        for (int i = 0; i < 6; ++i)
        {
            p.drawLine(QPointF(12 + i * 9, 25), QPointF(22 + i * 9, 31));
            p.drawLine(QPointF(12 + i * 9, 25), QPointF(34 + i * 9, 9));
        }
        QPainterPath path;
        path.moveTo(12, 22);
        path.cubicTo(26, 8, 44, 27, 64, 13);
        p.setPen(QPen(ink(QColor("#39c5cf")), 1.4));
        p.drawPath(path);
    }
    else if (kind == QLatin1String("horizon"))
    {
        p.setPen(QPen(ink(QColor("#4a5a68")), 1));
        p.setBrush(ink(QColor("#2b3a4a")));
        p.drawEllipse(QPointF(38, 19), 14, 14);
        p.setBrush(ink(QColor("#3a2f22")));
        p.drawChord(QRectF(24, 5, 28, 28), 180 * 16, 180 * 16);
        p.setPen(QPen(ink(QColor("#f2b134")), 1.4));
        p.drawLine(QPointF(25, 19), QPointF(51, 19));
        p.drawLine(QPointF(38, 17), QPointF(38, 21));
        p.drawLine(QPointF(31, 19), QPointF(45, 19));
    }
    else if (kind == QLatin1String("compass"))
    {
        p.setPen(QPen(ink(QColor("#4a5a68")), 1));
        p.drawEllipse(QPointF(38, 19), 13, 13);
        p.drawText(QRectF(35, 5, 8, 8), Qt::AlignCenter, QStringLiteral("N"));
        p.setPen(QPen(ink(QColor("#ff7b72")), 1.8));
        p.drawLine(QPointF(38, 19), QPointF(43, 10));
        p.setPen(QPen(ink(QColor("#c9d1d9")), 1));
        p.drawLine(QPointF(38, 19), QPointF(34, 27));
    }
    else if (kind == QLatin1String("gauge"))
    {
        p.setPen(QPen(ink(QColor("#4a5a68")), 1.2));
        p.drawArc(QRectF(18, 7, 40, 34), 200 * 16, -220 * 16);
        p.setPen(QPen(ink(QColor("#f2b134")), 1.6));
        p.drawLine(QPointF(38, 23), QPointF(51, 13));
        p.setBrush(ink(QColor("#f2b134")));
        p.drawEllipse(QPointF(38, 23), 2, 2);
    }
    return QIcon(pm);
}
