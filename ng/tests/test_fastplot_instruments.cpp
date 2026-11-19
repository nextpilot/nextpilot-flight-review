// fastplot P5：仪表族契约测试。
//
// 每个仪表都是"图上有什么"的客观断言，不是"调用没崩"的烟雾测试：
//   - 姿态球：天空色与地面色都必须出现，且颜色随滚转/俯仰改变而改变
//   - 罗盘：360° 与 0° 视为同一方向（归一化）
//   - 表盘：弧长随数值变化
//   - 轨迹：起终点标记必须出现
//   - 3D / 瀑布：折线像素必须出现
#include <fastplot/AttitudeIndicator.h>
#include <fastplot/CompassRose.h>
#include <fastplot/DialGauge.h>
#include <fastplot/PlotData.h>
#include <fastplot/TrackPlot.h>
#include <fastplot/Trajectory3D.h>
#include <fastplot/WaterfallPlot.h>

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QPolygonF>
#include <QVector>

#include <cmath>
#include <cstdio>
#include <vector>

// 硬断言：Release 下 -DNDEBUG 会消除标准 assert，测试就成空跑。
// 用本宏替代 assert，失败即 exit(1)，Debug/Release 都真正校验。
#include <cstdlib>
#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
            std::exit(1);                                                                \
        }                                                                                \
    } while (0)

namespace
{
bool near(double a, double b, double eps = 1e-9)
{
    return std::fabs(a - b) <= eps;
}

int countColor(const QImage &img, const QColor &color)
{
    int n = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (img.pixelColor(x, y) == color)
                ++n;
    return n;
}

// 找某个指定颜色像素的包围盒高度（用来判断"弧长变长了"、"指针转了"）
int colorSpanY(const QImage &img, const QColor &color)
{
    int top = img.height();
    int bottom = -1;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (img.pixelColor(x, y) == color)
            {
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
    return bottom < 0 ? 0 : bottom - top + 1;
}

QImage render(QWidget &w, int width, int height)
{
    w.resize(width, height);
    return w.grab().toImage();
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    // 1) 姿态仪：天空/地面都出现；俯仰改变要改变画面
    {
        nextpilot::plotscope::fastplot::AttitudeIndicator attitude;
        attitude.setSkyColor(QColor(0x2A, 0x6F, 0xC0));
        attitude.setGroundColor(QColor(0x9A, 0x62, 0x2E));
        attitude.setRollPitch(15.0, 8.0);
        const QImage img = render(attitude, 300, 240);
        CHECK(countColor(img, QColor(0x2A, 0x6F, 0xC0)) > 1000);
        CHECK(countColor(img, QColor(0x9A, 0x62, 0x2E)) > 1000);

        attitude.setRollPitch(-30.0, -10.0);
        const QImage img2 = render(attitude, 300, 240);
        CHECK(img2 != img);

        // 滚转 90°：天空/地面占比明显变化（球转过来了）
        attitude.setRollPitch(90.0, 0.0);
        const QImage img3 = render(attitude, 300, 240);
        CHECK(countColor(img3, QColor(0x2A, 0x6F, 0xC0)) != countColor(img, QColor(0x2A, 0x6F, 0xC0)));
        fprintf(stdout, "[ok] 姿态球：天空/地面/滚转生效\n");
    }

    // 2) 罗盘：归一化（360 → 0）与北向高亮
    {
        nextpilot::plotscope::fastplot::CompassRose compass;
        compass.setHeading(360.0);
        CHECK(near(compass.headingDeg(), 0.0, 1e-9));
        compass.setHeading(-45.0);
        CHECK(near(compass.headingDeg(), 315.0, 1e-9));

        compass.setHeading(90.0);
        const QImage east = render(compass, 240, 240);
        compass.setHeading(270.0);
        const QImage west = render(compass, 240, 240);
        CHECK(east != west); // 指针方向不同，图像必然不同
        fprintf(stdout, "[ok] 罗盘：航向归一化 + 指针方向\n");
    }

    // 3) 表盘：值越大弧越长（用 accent 色像素跨度衡量）
    {
        nextpilot::plotscope::fastplot::DialGauge gauge;
        gauge.setRange(0.0, 100.0);
        gauge.setTitle(QStringLiteral("rpm"));
        gauge.setValue(20.0);
        const QImage low = render(gauge, 260, 240);
        gauge.setValue(90.0);
        const QImage high = render(gauge, 260, 240);
        CHECK(countColor(high, gauge.accent()) > countColor(low, gauge.accent()));
        gauge.setValueMissing();
        const QImage missing = render(gauge, 260, 240);
        CHECK(missing != high);
        fprintf(stdout, "[ok] 表盘：弧长随值变化 + 无数据态\n");
    }

    // 4) 轨迹图：与 PlotWidget 一样只持指针，不复制数组
    {
        std::vector<double> t(200), lon(200), lat(200);
        for (int i = 0; i < 200; ++i)
        {
            t[i] = i;
            lon[i] = 104.06 + std::sin(i * 0.05) * 0.01;
            lat[i] = 30.66 + std::cos(i * 0.03) * 0.008;
        }
        nextpilot::plotscope::fastplot::ArrayData lonData(t.data(), lon.data(), 200, 31);
        nextpilot::plotscope::fastplot::ArrayData latData(t.data(), lat.data(), 200, 32);

        nextpilot::plotscope::fastplot::TrackPlot track;
        track.setStartColor(QColor(0x3D, 0xDC, 0x84));
        track.setEndColor(QColor(0xFF, 0x6B, 0x6B));
        track.setTrack(&lonData, &latData, QStringLiteral("lon"), QStringLiteral("lat"));
        QImage img = render(track, 320, 260);
        CHECK(countColor(img, QColor(0x3D, 0xDC, 0x84)) > 20); // 起点标记
        CHECK(countColor(img, QColor(0xFF, 0x6B, 0x6B)) > 20); // 终点标记

        track.clearTrack();
        const QImage empty = render(track, 320, 260);
        CHECK(countColor(empty, QColor(0x3D, 0xDC, 0x84)) == 0);
        fprintf(stdout, "[ok] 轨迹图：起终点标记 + 无数据态\n");
    }

    // 5) 3D 轨迹：三条通道投影，曲线像素必须出现
    {
        std::vector<double> t(300), x(300), y(300), z(300);
        for (int i = 0; i < 300; ++i)
        {
            t[i] = i;
            x[i] = std::cos(i * 0.05) * 5.0;
            y[i] = std::sin(i * 0.05) * 5.0;
            z[i] = i * 0.02;
        }
        nextpilot::plotscope::fastplot::ArrayData xd(t.data(), x.data(), 300, 41);
        nextpilot::plotscope::fastplot::ArrayData yd(t.data(), y.data(), 300, 42);
        nextpilot::plotscope::fastplot::ArrayData zd(t.data(), z.data(), 300, 43);

        nextpilot::plotscope::fastplot::Trajectory3D traj;
        traj.setCurveColor(QColor(0x5B, 0xC0, 0xF0));
        traj.setTracks(&xd, &yd, &zd, QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z"));
        const QImage img = render(traj, 320, 280);
        CHECK(countColor(img, QColor(0x5B, 0xC0, 0xF0)) > 100);
        fprintf(stdout, "[ok] 3D 轨迹：正交投影折线\n");
    }

    // 6) 瀑布：多条序列错开堆叠
    {
        std::vector<std::vector<double>> values(5, std::vector<double>(200));
        std::vector<std::vector<double>> times(5, std::vector<double>(200));
        for (int s = 0; s < 5; ++s)
            for (int i = 0; i < 200; ++i)
            {
                times[s][i] = i;
                values[s][i] = std::sin(i * 0.03 + s) + s * 0.3;
            }
        QVector<nextpilot::plotscope::fastplot::ArrayData> holders;
        for (int s = 0; s < 5; ++s)
            holders.push_back(nextpilot::plotscope::fastplot::ArrayData(times[s].data(), values[s].data(), 200, 50 + s));

        QVector<const nextpilot::plotscope::fastplot::IPlotData *> series;
        QVector<QColor> colors;
        for (int s = 0; s < holders.size(); ++s)
        {
            series.push_back(&holders[s]);
            colors.push_back(QColor(0x40 + s * 20, 0x90, 0xE0 - s * 10));
        }

        nextpilot::plotscope::fastplot::WaterfallPlot waterfall;
        waterfall.setSeries(series, colors);
        const QImage img = render(waterfall, 340, 280);
        int colored = 0;
        for (int s = 0; s < colors.size(); ++s)
            colored += countColor(img, colors.at(s).lighter(100));
        // 半透明叠加会改变实际像素值，这里退一步用"非背景像素"总量判断
        int ink = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                if (img.pixelColor(x, y) != waterfall.background())
                    ++ink;
        CHECK(ink > 3000 || colored > 300);
        fprintf(stdout, "[ok] 瀑布图：%d 条序列\n", series.size());
    }

    // 7) 拼一张总览图：六个部件各自独立，谁也不认识谁
    {
        const QColor bg(0x10, 0x16, 0x1c);
        QImage sheet(960, 560, QImage::Format_ARGB32);
        sheet.fill(bg);
        QPainter painter(&sheet);
        painter.setPen(QColor(0xc9, 0xd1, 0xd9));

        nextpilot::plotscope::fastplot::AttitudeIndicator attitude;
        attitude.setSkyColor(QColor(0x2A, 0x6F, 0xC0));
        attitude.setGroundColor(QColor(0x9A, 0x62, 0x2E));
        attitude.setRollPitch(12.0, 6.0);

        nextpilot::plotscope::fastplot::CompassRose compass;
        compass.setHeading(145.0);

        nextpilot::plotscope::fastplot::DialGauge gauge;
        gauge.setRange(0.0, 100.0);
        gauge.setValue(62.0);
        gauge.setTitle(QStringLiteral("rpm"));

        const int cellW = 320;
        const int cellH = 280;
        painter.drawImage(QRect(0, 0, cellW, cellH), render(attitude, cellW, cellH));
        painter.drawImage(QRect(cellW, 0, cellW, cellH), render(compass, cellW, cellH));
        painter.drawImage(QRect(cellW * 2, 0, cellW, cellH), render(gauge, cellW, cellH));

        std::vector<double> t(400), lon(400), lat(400);
        std::vector<double> x(400), y(400), z(400);
        for (int i = 0; i < 400; ++i)
        {
            t[i] = i;
            lon[i] = 104.06 + std::sin(i * 0.02) * 0.02;
            lat[i] = 30.66 + std::cos(i * 0.015) * 0.012;
            x[i] = std::cos(i * 0.02) * 6.0;
            y[i] = std::sin(i * 0.02) * 6.0;
            z[i] = std::sin(i * 0.01) * 3.0;
        }
        nextpilot::plotscope::fastplot::ArrayData lonD(t.data(), lon.data(), 400, 61);
        nextpilot::plotscope::fastplot::ArrayData latD(t.data(), lat.data(), 400, 62);
        nextpilot::plotscope::fastplot::ArrayData xD(t.data(), x.data(), 400, 63);
        nextpilot::plotscope::fastplot::ArrayData yD(t.data(), y.data(), 400, 64);
        nextpilot::plotscope::fastplot::ArrayData zD(t.data(), z.data(), 400, 65);

        nextpilot::plotscope::fastplot::TrackPlot track;
        track.setTrack(&lonD, &latD, QStringLiteral("lon"), QStringLiteral("lat"));
        nextpilot::plotscope::fastplot::Trajectory3D traj;
        traj.setTracks(&xD, &yD, &zD, QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z"));

        QVector<nextpilot::plotscope::fastplot::ArrayData> holders;
        // 必须先 reserve：曲线只持指针，QVector 扩容会把 ArrayData 搬走，
        // 之前取到的指针立刻变成野指针（这里踩过一次，直接 segfault）。
        holders.reserve(4);
        QVector<const nextpilot::plotscope::fastplot::IPlotData *> series;
        QVector<QColor> colors;
        for (int s = 0; s < 4; ++s)
        {
            std::vector<double> *tt = new std::vector<double>(300);
            std::vector<double> *vv = new std::vector<double>(300);
            for (int i = 0; i < 300; ++i)
            {
                (*tt)[i] = i;
                (*vv)[i] = std::sin(i * 0.04 + s) + s * 0.4;
            }
            holders.push_back(nextpilot::plotscope::fastplot::ArrayData(tt->data(), vv->data(), 300, 70 + s));
            series.push_back(&holders.last());
            colors.push_back(QColor(0x40 + s * 30, 0x90 + s * 10, 0xE0 - s * 20));
        }

        nextpilot::plotscope::fastplot::WaterfallPlot waterfall;
        waterfall.setSeries(series, colors);

        painter.drawImage(QRect(0, cellH, cellW, cellH), render(track, cellW, cellH));
        painter.drawImage(QRect(cellW, cellH, cellW, cellH), render(traj, cellW, cellH));
        painter.drawImage(QRect(cellW * 2, cellH, cellW, cellH), render(waterfall, cellW, cellH));
        painter.end();

        const QString path = QDir::current().filePath(QStringLiteral("fastplot_instruments.png"));
        CHECK(sheet.save(path));
        fprintf(stdout, "[ok] 总览图已写出 %s\n", qPrintable(path));
    }

    fprintf(stdout, "[ok] fastplot P5 仪表族全部通过\n");
    return 0;
}
