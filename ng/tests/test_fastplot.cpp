// fastplot P1 契约测试：视口映射、刻度划分、布局边距、图层缓存渲染。
//
// 不引入 QtTest —— 用 CHECK 硬断言（Release 下 assert 会被 NDEBUG 消除，不能用）。
#include <fastplot/BarChart.h>
#include <fastplot/Histogram.h>
#include <fastplot/SpectrumPlot.h>
#include <fastplot/Axis.h>
#include <fastplot/Picker.h>
#include <fastplot/PlotWidget.h>
#include <fastplot/ScaleDiv.h>
#include <fastplot/Viewport.h>
#include <fastplot/Zoomer.h>

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QJsonObject>

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

constexpr double kPi = 3.14159265358979323846;

bool near(double a, double b, double eps = 1e-9)
{
    return std::fabs(a - b) <= eps;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    // 1) 视口映射：四角必须精确落在绘图区边界上
    {
        fastplot::Viewport vp;
        vp.setPlotRect(QRectF(10, 10, 100, 50));
        vp.setXRange(0.0, 1.0);
        vp.setYRange(0.0, 1.0);

        CHECK(near(vp.xToPixel(0.0), 10.0));
        CHECK(near(vp.xToPixel(1.0), 110.0));
        // y 向上增大：上限在顶边，下限在底边
        CHECK(near(vp.yToPixel(1.0), 10.0));
        CHECK(near(vp.yToPixel(0.0), 60.0));
        // 往返一致
        CHECK(near(vp.pixelToX(vp.xToPixel(0.37)), 0.37));
        CHECK(near(vp.pixelToY(vp.yToPixel(0.42)), 0.42));

        // lo == hi 自动展开，不除零
        fastplot::Viewport degenerate;
        degenerate.setPlotRect(QRectF(0, 0, 10, 10));
        degenerate.setXRange(5.0, 5.0);
        CHECK(degenerate.xUpper() > degenerate.xLower());
        CHECK(std::isfinite(degenerate.xToPixel(5.0)));
        fprintf(stdout, "[ok] Viewport：四角映射精确 + 退化范围自动展开\n");
    }

    // 2) 刻度划分：步长是"好看数"，标签无浮点尾巴
    {
        const fastplot::ScaleDiv div = fastplot::ScaleDiv::linear(0.0, 2.0, 8);
        CHECK(div.majorStep > 0.0);
        // 0~2 分 8 段 → 步长应落在 0.2 / 0.25 这类"好看数"
        const bool niceStep = near(div.majorStep, 0.2) || near(div.majorStep, 0.25)
            || near(div.majorStep, 0.5);
        CHECK(niceStep);
        CHECK(div.lower <= 0.0 + 1e-12);
        CHECK(div.upper >= 2.0 - 1e-12);

        int majors = 0;
        for (const fastplot::Tick &t : div.ticks)
        {
            if (!t.major)
                continue;
            ++majors;
            CHECK(!t.label.isEmpty());
            CHECK(!t.label.contains(QStringLiteral("00000000")));
        }
        CHECK(majors >= 5);

        // 标签格式：0.25 不带多余 0，-0 归一成 0
        CHECK(fastplot::ScaleDiv::formatValue(0.25, 0.25) == QStringLiteral("0.25"));
        CHECK(fastplot::ScaleDiv::formatValue(-0.0, 0.25) == QStringLiteral("0"));
        fprintf(stdout, "[ok] ScaleDiv：步长 %.3f，主刻度 %d 个，标签 %.1f\n",
                div.majorStep, majors, div.ticks.isEmpty() ? 0.0 : div.ticks.first().value);
    }

    // 3) 控件布局：边距由轴自己算出来，绘图区不等于整个窗口
    {
        fastplot::PlotWidget plot;
        plot.resize(800, 400);
        plot.setXRange(0.0, 2.0);
        plot.setYRange(-1.0, 1.0);

        const QRectF pr = plot.plotRect();
        CHECK(pr.width() > 600 && pr.height() > 300);
        CHECK(pr.left() >= 24);            // 左轴标签占位
        CHECK(pr.bottom() <= 400 - 16);    // 下轴标签占位
        CHECK(plot.axis(fastplot::AxisSide::Bottom).scaleDiv().upper >= 2.0);
        CHECK(plot.axis(fastplot::AxisSide::Left).scaleDiv().lower <= -1.0);
        // 右轴默认隐藏 → 右边距应为 0
        CHECK(pr.right() >= 800 - 24);

        // 打开右轴后，右边距增大
        const double widthBefore = plot.plotRect().width();
        plot.axis(fastplot::AxisSide::Right).setVisible(true);
        plot.replot();
        CHECK(plot.plotRect().width() < widthBefore);
        fprintf(stdout, "[ok] PlotWidget：边距 %.0f/%.0f，双轴切换后绘图区收窄\n",
                pr.left(), 400 - pr.bottom());
    }

    // 4) 渲染：网格真的画出来了，且静态缓存可复用
    {
        fastplot::PlotWidget plot;
        plot.resize(800, 400);
        plot.setXRange(0.0, 2.0);
        plot.setYRange(-1.0, 1.0);

        const QImage img = plot.grabImage();
        CHECK(img.size() == QSize(800, 400));

        const QColor bg = plot.background();
        const QRectF pr = plot.plotRect();

        // 取下轴第一个主刻度，横向扫 ±2 像素找网格线
        const fastplot::ScaleDiv &xDiv = plot.axis(fastplot::AxisSide::Bottom).scaleDiv();
        int hit = 0;
        for (const fastplot::Tick &t : xDiv.ticks)
        {
            if (!t.major || t.value < 0.0)
                continue;
            const int px = static_cast<int>(std::lround(plot.viewport().xToPixel(t.value)));
            const int py = static_cast<int>(std::lround((pr.top() + pr.bottom()) / 2.0));
            for (int dx = -2; dx <= 2; ++dx)
            {
                const QColor c = img.pixelColor(std::clamp(px + dx, 0, 799), py);
                if (c != bg)
                {
                    ++hit;
                    break;
                }
            }
        }
        CHECK(hit >= 3);

        // 导出 PNG，供肉眼验收（ctest 的工作目录是构建目录）
        const QString path = QDir::current().filePath(QStringLiteral("fastplot_p1.png"));
        CHECK(img.save(path));
        fprintf(stdout, "[ok] 渲染：%d 条主网格可见，PNG 已写出 %s\n", hit,
                path.toUtf8().constData());
    }

    // 5) 像素列聚合：3000 点压成 ~700 列，min/max 不丢峰
    {
        const int n = 3000;
        std::vector<double> xs(n), ys(n);
        for (int i = 0; i < n; ++i)
        {
            xs[i] = i * 0.001;
            ys[i] = std::sin(i * 0.02);
        }
        fastplot::ArrayData data(xs.data(), ys.data(), n, 7);

        fastplot::Viewport vp;
        vp.setPlotRect(QRectF(0, 0, 700, 300));
        vp.setXRange(0.0, 3.0);
        vp.setYRange(-1.2, 1.2);

        fastplot::LodCache cache;
        const QVector<fastplot::LodPoint> &pts = cache.points(data, vp);
        CHECK(!pts.isEmpty());
        CHECK(pts.size() <= 700);
        // 峰值必须活下来：包络保留列内 min/max，不会被"平均"掉
        double lo = 0.0, hi = 0.0;
        for (const fastplot::LodPoint &pt : pts)
        {
            CHECK(pt.min <= pt.max);
            lo = std::min(lo, pt.min);
            hi = std::max(hi, pt.max);
        }
        CHECK(hi >= 0.99 && lo <= -0.99);

        // 同一视口再画一次：必须命中缓存，不重扫
        const int rebuildsBefore = cache.rebuilds();
        cache.points(data, vp);
        CHECK(cache.rebuilds() == rebuildsBefore);

        // 视口变了就得重算
        vp.setXRange(0.0, 1.5);
        cache.points(data, vp);
        CHECK(cache.rebuilds() == rebuildsBefore + 1);
        fprintf(stdout, "[ok] LodCache：3000 点 → %d 列，命中缓存不重扫\n", int(pts.size()));
    }

    // 6) 曲线渲染：画出来的确实是数据（不是空坐标系）
    {
        const int n = 3000;
        std::vector<double> xs(n), ys(n);
        for (int i = 0; i < n; ++i)
        {
            xs[i] = i * 0.001;
            ys[i] = std::sin(i * 0.02) * 0.5;
        }
        fastplot::ArrayData data(xs.data(), ys.data(), n, 11);

        const QColor ink(0x5b, 0xc0, 0xf0);
        fastplot::PlotWidget plot;
        plot.resize(800, 400);
        plot.addCurve(fastplot::Curve::fromData(&data, ink, 1.4));
        CHECK(plot.autoScale());

        const QImage img = plot.grabImage();
        int curvePixels = 0;
        for (int y = 0; y < img.height(); ++y)
        {
            for (int x = 0; x < img.width(); ++x)
            {
                if (img.pixelColor(x, y) == ink)
                    ++curvePixels;
            }
        }
        CHECK(curvePixels > 200);

        const QString path = QDir::current().filePath(QStringLiteral("fastplot_p2.png"));
        CHECK(img.save(path));
        fprintf(stdout, "[ok] 曲线渲染：%d 个曲线像素，PNG 已写出 %s\n", curvePixels,
                path.toUtf8().constData());
    }

    // 6b) 散点模式：只画符号不连线
    {
        const int n = 200;
        std::vector<double> xs(n), ys(n);
        for (int i = 0; i < n; ++i)
        {
            xs[i] = i * 0.05;
            ys[i] = std::cos(i * 0.3) * 0.6 + 0.2 * std::sin(i * 1.7);
        }
        fastplot::ArrayData data(xs.data(), ys.data(), n, 31);

        const QColor ink(0x8A, 0xD8, 0x6E);
        fastplot::PlotWidget plot;
        plot.resize(800, 400);
        auto &curve = plot.addCurve(fastplot::Curve::fromData(&data, ink, 1.4));
        curve.setScatter(true);
        curve.setSymbol(fastplot::Symbol::Diamond);
        curve.setSymbolSize(6.0);
        CHECK(plot.autoScale());

        const QImage img = plot.grabImage();
        int markerPixels = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                if (img.pixelColor(x, y) == ink)
                    ++markerPixels;
        CHECK(markerPixels > 100);

        const QString path = QDir::current().filePath(QStringLiteral("fastplot_scatter.png"));
        CHECK(img.save(path));
        fprintf(stdout, "[ok] 散点模式：%d 个符号像素，PNG 已写出 %s\n", markerPixels,
                path.toUtf8().constData());
    }

    // 7) 交互部件：缩放锚点不动、框选铺满、读数取最近样本
    {
        const int n = 3000;
        std::vector<double> xs(n), ys(n);
        for (int i = 0; i < n; ++i)
        {
            xs[i] = i * 0.001;
            ys[i] = std::sin(i * 0.02);
        }
        fastplot::ArrayData data(xs.data(), ys.data(), n, 21);

        fastplot::PlotWidget plot;
        plot.resize(800, 400);
        plot.addCurve(fastplot::Curve::fromData(&data, QColor(0x1F, 0x6F, 0xC0), 1.4));
        plot.setXRange(0.0, 3.0);
        plot.setYRange(-1.2, 1.2);

        fastplot::Zoomer zoomer(&plot);
        const QPointF anchor(400.0, 200.0);
        const double ax = plot.viewport().pixelToX(anchor.x());
        const double ay = plot.viewport().pixelToY(anchor.y());
        const double spanXBefore = plot.viewport().xUpper() - plot.viewport().xLower();

        zoomer.zoomBy(0.5, anchor);
        // 锚点数据坐标不动 = 缩放"绕着鼠标"进行，不是绕着中心
        CHECK(near(plot.viewport().pixelToX(anchor.x()), ax, 1e-9));
        CHECK(near(plot.viewport().pixelToY(anchor.y()), ay, 1e-9));
        const double spanXAfter = plot.viewport().xUpper() - plot.viewport().xLower();
        CHECK(spanXAfter < spanXBefore * 0.55);

        // 框选：像素矩形放大后跨度必须明显变小
        const double spanBeforeBand = plot.viewport().xUpper() - plot.viewport().xLower();
        zoomer.zoomToPixels(QRect(200, 100, 200, 100));
        const double spanAfterBand = plot.viewport().xUpper() - plot.viewport().xLower();
        CHECK(spanAfterBand < spanBeforeBand);

        // 复位回接手时的视口
        zoomer.resetZoom();
        CHECK(near(plot.viewport().xUpper() - plot.viewport().xLower(), spanXBefore, 1e-6));

        // 读数：最近样本落在曲线值域内
        double sx = 0.0;
        double sy = 0.0;
        CHECK(plot.nearestSample(QPointF(400.0, 120.0), &sx, &sy));
        CHECK(sy >= -1.01 && sy <= 1.01);

        // 叠加层（十字光标 + 读数）确实画到了图上
        plot.setHover(QPointF(sx, sy), true);
        plot.setOverlayText(QStringLiteral("t = 1.234 s"));
        const QImage img = plot.grabImage();
        int overlayPixels = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                if (img.pixelColor(x, y).alpha() > 0 && img.pixelColor(x, y) != plot.background())
                    ++overlayPixels;
        CHECK(overlayPixels > 100);

        fprintf(stdout, "[ok] 交互：锚点缩放/框选/复位/最近样本/叠加层\n");
    }

    // 8) 双轴 / 图例 / 导出 / 状态复原
    {
        // 左轴曲线：正弦 ±0.5（占满左轴）；右轴曲线：约 200 的一条平带（只占右轴中间一小段）。
        // 两条曲线像素不能"看起来一样"——否则无法证明各走各的映射。
        QVector<fastplot::LodPoint> left;
        QVector<fastplot::LodPoint> right;
        for (int i = 0; i < 400; ++i)
        {
            const double x = i * 0.016;
            left.push_back(fastplot::LodPoint{x, 0.5 * std::sin(x), 0.55 * std::sin(x) + 0.02});
            right.push_back(fastplot::LodPoint{x, 199.5, 200.5});
        }

        const QColor leftInk(0x1F, 0x6F, 0xC0);
        const QColor rightInk(0xE0, 0x6C, 0x00);

        fastplot::PlotWidget plot;
        plot.resize(800, 400);
        plot.setXRange(0.0, 6.4);
        plot.setYRange(-1.0, 1.0);
        plot.setSecondAxis(true);
        plot.setYRange2(190.0, 210.0);

        auto &c1 = plot.addCurve(fastplot::Curve::fromLod(left, leftInk, 1.0));
        c1.setName(QStringLiteral("roll"));
        auto &c2 = plot.addCurve(fastplot::Curve::fromLod(right, rightInk, 1.0));
        c2.setName(QStringLiteral("alt"));
        c2.setYAxis(fastplot::AxisSide::Right);

        CHECK(plot.hasSecondAxis());
        CHECK(near(plot.y2Upper(), 210.0) && near(plot.y2Lower(), 190.0));

        // 先关图例测曲线：图例色块也在左上角，会把"右侧窄带"的量测带偏。
        plot.setLegendVisible(false);
        const QImage img = plot.grabImage();
        int leftPixels = 0;
        int rightPixels = 0;
        int leftTop = img.height();
        int leftBottom = 0;
        int rightTop = img.height();
        int rightBottom = 0;
        for (int y = 0; y < img.height(); ++y)
        {
            for (int x = 0; x < img.width(); ++x)
            {
                const QColor c = img.pixelColor(x, y);
                if (c == leftInk)
                {
                    ++leftPixels;
                    leftTop = std::min(leftTop, y);
                    leftBottom = std::max(leftBottom, y);
                }
                else if (c == rightInk)
                {
                    ++rightPixels;
                    rightTop = std::min(rightTop, y);
                    rightBottom = std::max(rightBottom, y);
                }
            }
        }
        CHECK(leftPixels > 500);
        CHECK(rightPixels > 500);
        // 关键：右轴 190~210 的 200 落在绘图区中部，只占很薄一条；左轴正弦铺满高度。
        CHECK(rightBottom - rightTop < 40);
        CHECK(leftBottom - leftTop > 0.45 * img.height());

        // 图例：开与不开必须画出不同结果（色块 + 名字确实是画上去的）
        plot.setLegendVisible(true);
        const QImage withLegend = plot.grabImage();
        CHECK(withLegend != img);

        const QString png = QDir::current().filePath(QStringLiteral("fastplot_p4.png"));
        const QString csv = QDir::current().filePath(QStringLiteral("fastplot_p4.csv"));
        CHECK(plot.exportPng(png));
        CHECK(plot.exportCsv(csv));
        // CSV 必须同时含两条曲线（LOD 输出 x,min,max）
        QFile f(csv);
        CHECK(f.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString text = QString::fromUtf8(f.readAll());
        CHECK(text.contains(QStringLiteral("roll")));
        CHECK(text.contains(QStringLiteral("alt")));
        CHECK(text.contains(QStringLiteral("x,min,max")));

        // 状态往返：范围/双轴/图例都要还原
        const QJsonObject state = plot.saveState();
        fastplot::PlotWidget restored;
        restored.resize(800, 400);
        CHECK(restored.restoreState(state));
        CHECK(near(restored.viewport().xLower(), plot.viewport().xLower()));
        CHECK(near(restored.viewport().xUpper(), plot.viewport().xUpper()));
        CHECK(restored.hasSecondAxis());
        CHECK(near(restored.y2Upper(), 210.0) && near(restored.y2Lower(), 190.0));
        CHECK(restored.legendVisible());

        fprintf(stdout, "[ok] 双轴/图例/导出 PNG+CSV/状态复原（左 %d px、右 %d px 高 %d px）\n",
                leftPixels, rightPixels, rightBottom - rightTop);
    }

    // 6c) 柱状图 / 直方图 / 频谱
    {
        auto nearInkCount = [](QWidget &w, const QColor &ink) {
            const QImage img = w.grab().toImage();
            int n = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                {
                    const QColor c = img.pixelColor(x, y);
                    if (std::abs(c.red() - ink.red()) < 40 &&
                        std::abs(c.green() - ink.green()) < 40 &&
                        std::abs(c.blue() - ink.blue()) < 40)
                        ++n;
                }
            return n;
        };

        // 柱状图：三个类别，含一个负值。
        {
            fastplot::BarChart bar;
            bar.resize(400, 300);
            bar.setBars({QStringLiteral("roll"), QStringLiteral("pitch"), QStringLiteral("rpm")},
                        {12.0, -3.0, 86.0},
                        {QColor(0x5b, 0xc0, 0xf0), QColor(0xf0, 0x9d, 0x5b),
                         QColor(0x8a, 0xd8, 0x6e)});
            const QImage img = bar.grab().toImage();
            CHECK(nearInkCount(bar, QColor(0x5b, 0xc0, 0xf0)) > 30);
            CHECK(img.save(QDir::current().filePath(QStringLiteral("fastplot_bar.png"))));
            fprintf(stdout, "[ok] 柱状图：三柱（含负值）出图\n");
        }

        // 直方图：一组双峰分布。
        {
            const int n = 2000;
            std::vector<double> xs(n), ys(n);
            for (int i = 0; i < n; ++i)
            {
                xs[i] = i * 0.01;
                const double phase = i < n / 2 ? 0.0 : 3.0;
                ys[i] = phase + std::sin(i * 0.1) * 0.3;
            }
            fastplot::ArrayData data(xs.data(), ys.data(), n, 41);
            fastplot::Histogram hist;
            hist.resize(400, 300);
            hist.setData(&data);
            hist.setBins(32);
            const QImage img = hist.grab().toImage();
            const int colored = nearInkCount(hist, QColor(0x5b, 0xc0, 0xf0));
            CHECK(colored > 50);
            CHECK(img.save(QDir::current().filePath(QStringLiteral("fastplot_hist.png"))));
            fprintf(stdout, "[ok] 直方图：32 箱双峰分布出图\n");
        }

        // 频谱：5 Hz 正弦 + 小幅噪声，dt=0.01s。
        {
            const int n = 256;
            std::vector<double> xs(n), ys(n);
            for (int i = 0; i < n; ++i)
            {
                xs[i] = i * 0.01;
                ys[i] = std::sin(2 * kPi * 5.0 * xs[i]) + 0.1 * std::sin(i * 1.3);
            }
            fastplot::ArrayData data(xs.data(), ys.data(), n, 43);
            fastplot::SpectrumPlot spec;
            spec.resize(400, 300);
            spec.setData(&data);
            spec.setSampleDt(0.01);
            const QImage img = spec.grab().toImage();
            const int colored = nearInkCount(spec, QColor(0x5b, 0xc0, 0xf0));
            CHECK(colored > 20);
            CHECK(img.save(QDir::current().filePath(QStringLiteral("fastplot_spectrum.png"))));
            fprintf(stdout, "[ok] 频谱图：256 点单边幅值谱出图\n");
        }
    }

    fprintf(stdout, "[ok] fastplot P1~P4 全部通过\n");
    return 0;
}
