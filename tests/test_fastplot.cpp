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
#include <QEvent>
#include <QImage>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPainter>
#include <QWheelEvent>

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
        nextpilot::plotscope::fastplot::Viewport vp;
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
        nextpilot::plotscope::fastplot::Viewport degenerate;
        degenerate.setPlotRect(QRectF(0, 0, 10, 10));
        degenerate.setXRange(5.0, 5.0);
        CHECK(degenerate.xUpper() > degenerate.xLower());
        CHECK(std::isfinite(degenerate.xToPixel(5.0)));
        fprintf(stdout, "[ok] Viewport：四角映射精确 + 退化范围自动展开\n");
    }

    // 2) 刻度划分：步长是"好看数"，标签无浮点尾巴
    {
        const nextpilot::plotscope::fastplot::ScaleDiv div = nextpilot::plotscope::fastplot::ScaleDiv::linear(0.0, 2.0, 8);
        CHECK(div.majorStep > 0.0);
        // 0~2 分 8 段 → 步长应落在 0.2 / 0.25 这类"好看数"
        const bool niceStep = near(div.majorStep, 0.2) || near(div.majorStep, 0.25)
            || near(div.majorStep, 0.5);
        CHECK(niceStep);
        CHECK(div.lower <= 0.0 + 1e-12);
        CHECK(div.upper >= 2.0 - 1e-12);

        int majors = 0;
        for (const nextpilot::plotscope::fastplot::Tick &t : div.ticks)
        {
            if (!t.major)
                continue;
            ++majors;
            CHECK(!t.label.isEmpty());
            CHECK(!t.label.contains(QStringLiteral("00000000")));
        }
        CHECK(majors >= 5);

        // 标签格式：0.25 不带多余 0，-0 归一成 0
        CHECK(nextpilot::plotscope::fastplot::ScaleDiv::formatValue(0.25, 0.25) == QStringLiteral("0.25"));
        CHECK(nextpilot::plotscope::fastplot::ScaleDiv::formatValue(-0.0, 0.25) == QStringLiteral("0"));
        fprintf(stdout, "[ok] ScaleDiv：步长 %.3f，主刻度 %d 个，标签 %.1f\n",
                div.majorStep, majors, div.ticks.isEmpty() ? 0.0 : div.ticks.first().value);
    }

    // 3) 控件布局：边距由轴自己算出来，绘图区不等于整个窗口
    {
        nextpilot::plotscope::fastplot::PlotWidget plot;
        plot.resize(800, 400);
        plot.setXRange(0.0, 2.0);
        plot.setYRange(-1.0, 1.0);

        const QRectF pr = plot.plotRect();
        CHECK(pr.width() > 600 && pr.height() > 300);
        CHECK(pr.left() >= 24);            // 左轴标签占位
        CHECK(pr.bottom() <= 400 - 16);    // 下轴标签占位
        CHECK(plot.axis(nextpilot::plotscope::fastplot::AxisSide::Bottom).scaleDiv().upper >= 2.0);
        CHECK(plot.axis(nextpilot::plotscope::fastplot::AxisSide::Left).scaleDiv().lower <= -1.0);
        // 右轴默认隐藏 → 右边距应为 0
        CHECK(pr.right() >= 800 - 24);

        // 打开右轴后，右边距增大
        const double widthBefore = plot.plotRect().width();
        plot.axis(nextpilot::plotscope::fastplot::AxisSide::Right).setVisible(true);
        plot.replot();
        CHECK(plot.plotRect().width() < widthBefore);
        fprintf(stdout, "[ok] PlotWidget：边距 %.0f/%.0f，双轴切换后绘图区收窄\n",
                pr.left(), 400 - pr.bottom());
    }

    // 4) 渲染：网格真的画出来了，且静态缓存可复用
    {
        nextpilot::plotscope::fastplot::PlotWidget plot;
        plot.resize(800, 400);
        plot.setXRange(0.0, 2.0);
        plot.setYRange(-1.0, 1.0);

        const QImage img = plot.grabImage();
        CHECK(img.size() == QSize(800, 400));

        const QColor bg = plot.background();
        const QRectF pr = plot.plotRect();

        // 取下轴第一个主刻度，横向扫 ±2 像素找网格线
        const nextpilot::plotscope::fastplot::ScaleDiv &xDiv = plot.axis(nextpilot::plotscope::fastplot::AxisSide::Bottom).scaleDiv();
        int hit = 0;
        for (const nextpilot::plotscope::fastplot::Tick &t : xDiv.ticks)
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
        nextpilot::plotscope::fastplot::ArrayData data(xs.data(), ys.data(), n, 7);

        nextpilot::plotscope::fastplot::Viewport vp;
        vp.setPlotRect(QRectF(0, 0, 700, 300));
        vp.setXRange(0.0, 3.0);
        vp.setYRange(-1.2, 1.2);

        nextpilot::plotscope::fastplot::LodCache cache;
        const QVector<nextpilot::plotscope::fastplot::LodPoint> &pts = cache.points(data, vp);
        CHECK(!pts.isEmpty());
        CHECK(pts.size() <= 700);
        // 峰值必须活下来：包络保留列内 min/max，不会被"平均"掉
        double lo = 0.0, hi = 0.0;
        for (const nextpilot::plotscope::fastplot::LodPoint &pt : pts)
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

        // 1.3 revision 传导：同一数组、revision 变化也必须重算（内容变了就失效）。
        // 这证明 LodCache 用的是 IPlotData::revision() 而非指针身份做缓存键。
        const int revRebuildsBefore = cache.rebuilds();
        nextpilot::plotscope::fastplot::ArrayData dataNewRev(xs.data(), ys.data(), n, 99);
        cache.points(dataNewRev, vp);
        CHECK(cache.rebuilds() == revRebuildsBefore + 1);
        // 新 revision 再次命中，不重算
        cache.points(dataNewRev, vp);
        CHECK(cache.rebuilds() == revRebuildsBefore + 1);
        fprintf(stdout, "[ok] LodCache：3000 点 → %d 列，命中缓存不重扫，revision 变化触发重算\n",
                int(pts.size()));
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
        nextpilot::plotscope::fastplot::ArrayData data(xs.data(), ys.data(), n, 11);

        const QColor ink(0x5b, 0xc0, 0xf0);
        nextpilot::plotscope::fastplot::PlotWidget plot;
        plot.resize(800, 400);
        plot.addCurve(nextpilot::plotscope::fastplot::Curve::fromData(&data, ink, 1.4));
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
        nextpilot::plotscope::fastplot::ArrayData data(xs.data(), ys.data(), n, 31);

        const QColor ink(0x8A, 0xD8, 0x6E);
        nextpilot::plotscope::fastplot::PlotWidget plot;
        plot.resize(800, 400);
        auto &curve = plot.addCurve(nextpilot::plotscope::fastplot::Curve::fromData(&data, ink, 1.4));
        curve.setScatter(true);
        curve.setSymbol(nextpilot::plotscope::fastplot::Symbol::Diamond);
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

    // 6c) NaN gap 缺测断线（#IKXFP8）：全 null 桶（NaN,NaN）与空桶（时间间隙）
    // 都必须断开，不把缺测区间连成直线；正常数据不留假 gap。
    {
        const QColor ink(0xE4, 0x57, 0x2E); // 独占色，避免与其他元素混淆

        // 缺测像素检查：只统计 (x0,x1) 开区间内、曲线色带 y 邻域里的曲线像素。
        // 端点各让 3px：线宽 1.4 的半宽 0.7px + 抗锯齿外溢约 1px + 1px 安全余量 ≈ 3px，
        // 仅排除端点合法样本自身的色带；gap 内部一列都不让，避免把真断线漏过去。
        auto inkPixelsBetween = [&](const QImage &img, double xa, double xb, double yLo, double yHi) {
            int hits = 0;
            const int colA = int(std::ceil(std::min(xa, xb))) + 3; // 端点各让 3px
            const int colB = int(std::floor(std::max(xa, xb))) - 3;
            for (int x = colA; x <= colB; ++x)
                for (int y = int(yLo); y <= int(yHi); ++y)
                    if (img.pixelColor(x, y) == ink)
                        ++hits;
            return hits;
        };

        // A) 全 null 桶 → Rust 输出 (NaN,NaN) 的 LodPoint：断开且不画竖线。
        {
            QVector<nextpilot::plotscope::fastplot::LodPoint> pts;
            for (int i = 0; i <= 20; ++i)
                pts.push_back({0.05 * i, -0.5, 0.5});
            pts.push_back({1.05, std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::quiet_NaN()}); // 全 null 桶
            for (int i = 0; i <= 20; ++i)
                pts.push_back({1.1 + 0.05 * i, 0.2, 0.8});

            nextpilot::plotscope::fastplot::PlotWidget plot;
            plot.resize(800, 400);
            plot.addCurve(nextpilot::plotscope::fastplot::Curve::fromLod(pts, ink, 1.4));
            plot.setXRange(0.0, 2.2);
            plot.setYRange(-1.0, 1.0);

            const QImage img = plot.grabImage();
            const auto &vp = plot.viewport();
            // gap 区间 [1.0, 1.1] 内不允许任何曲线像素（NaN 桶竖线也必须消失）
            const int hits = inkPixelsBetween(img, vp.xToPixel(1.0), vp.xToPixel(1.1),
                                              vp.plotRect().top(), vp.plotRect().bottom());
            CHECK(hits == 0);
            // 更硬的内部断言：不经 helper 让位，直接扫 gap 内部子区间 [1.02,1.08]。
            // 该子区间完全落在 gap 内，任何 ink 都意味着断线逻辑把缺测区间连成了线。
            const int innerA = int(std::ceil(vp.xToPixel(1.02)));
            const int innerB = int(std::floor(vp.xToPixel(1.08)));
            const int innerWidth = innerB - innerA + 1;
            int innerHits = 0;
            for (int x = innerA; x <= innerB; ++x)
                for (int y = int(vp.plotRect().top()); y <= int(vp.plotRect().bottom()); ++y)
                    if (img.pixelColor(x, y) == ink)
                        ++innerHits;
            fprintf(stdout, "[ok] A 组 gap 内部子区间 [1.02,1.08]：宽 %d 列，ink=%d\n",
                    innerWidth, innerHits);
            CHECK(innerWidth >= 10);
            CHECK(innerHits == 0);
            // 两侧曲线本体还在
            int body = 0;
            for (int x = 0; x < img.width(); ++x)
                for (int y = 0; y < img.height(); ++y)
                    if (img.pixelColor(x, y) == ink)
                        ++body;
            CHECK(body > 200);
        }

        // B) 空桶（无 NaN 点，仅时间间隙 1.0 > 中位数 0.05 × 3）：同样断开。
        {
            QVector<nextpilot::plotscope::fastplot::LodPoint> pts;
            for (int i = 0; i <= 20; ++i)
                pts.push_back({0.05 * i, -0.5, 0.5});
            for (int i = 0; i <= 20; ++i)
                pts.push_back({2.0 + 0.05 * i, 0.2, 0.8}); // [1.0, 2.0) 无样本

            nextpilot::plotscope::fastplot::PlotWidget plot;
            plot.resize(800, 400);
            plot.addCurve(nextpilot::plotscope::fastplot::Curve::fromLod(pts, ink, 1.4));
            plot.setXRange(0.0, 3.0);
            plot.setYRange(-1.0, 1.0);

            const QImage img = plot.grabImage();
            const auto &vp = plot.viewport();
            const int hits = inkPixelsBetween(img, vp.xToPixel(1.05), vp.xToPixel(1.95),
                                              vp.plotRect().top(), vp.plotRect().bottom());
            CHECK(hits == 0);
        }

        // C) 正常连续数据不留假 gap：每列都有样本 → 每列都有竖线，无断口。
        {
            QVector<nextpilot::plotscope::fastplot::LodPoint> pts;
            for (int i = 0; i <= 700; ++i)
                pts.push_back({0.01 * i, std::sin(0.1 * i) - 0.1, std::sin(0.1 * i) + 0.1});

            nextpilot::plotscope::fastplot::PlotWidget plot;
            plot.resize(800, 400);
            plot.addCurve(nextpilot::plotscope::fastplot::Curve::fromLod(pts, ink, 1.4));
            plot.setXRange(0.0, 7.0);
            plot.setYRange(-1.2, 1.2);

            const QImage img = plot.grabImage();
            const auto &vp = plot.viewport();
            // 数据覆盖的列区间内，有曲线像素的列数应接近全覆盖（无连续断口）
            const int colA = int(vp.xToPixel(0.02)), colB = int(vp.xToPixel(6.9));
            int withInk = 0, total = 0;
            for (int x = colA; x <= colB; ++x)
            {
                ++total;
                for (int y = 0; y < img.height(); ++y)
                    if (img.pixelColor(x, y) == ink)
                    {
                        ++withInk;
                        break;
                    }
            }
            CHECK(total > 300);
            CHECK(withInk >= total * 95 / 100); // ≥95% 列有曲线，无系统性断口
        }

        // D) 稀疏 ArrayData 含 NaN y：polyline 在缺测处断开。
        {
            const int n = 40;
            std::vector<double> xs(n), ys(n);
            for (int i = 0; i < n; ++i)
            {
                xs[i] = 0.1 * i;
                ys[i] = (i >= 15 && i <= 24) ? std::numeric_limits<double>::quiet_NaN()
                                             : std::sin(0.5 * i);
            }
            nextpilot::plotscope::fastplot::ArrayData data(xs.data(), ys.data(), n, 41);

            nextpilot::plotscope::fastplot::PlotWidget plot;
            plot.resize(800, 400);
            plot.addCurve(nextpilot::plotscope::fastplot::Curve::fromData(&data, ink, 1.4));
            plot.setXRange(0.0, 3.9);
            plot.setYRange(-1.5, 1.5);

            const QImage img = plot.grabImage();
            const auto &vp = plot.viewport();
            const int hits = inkPixelsBetween(img, vp.xToPixel(1.55), vp.xToPixel(2.35),
                                              vp.plotRect().top(), vp.plotRect().bottom());
            CHECK(hits == 0);
        }

        // E) 稀疏路径静默缺测（无 NaN 标记，仅 x 时间跳跃）：同样断开。
        // 与 LOD 路径同阈值（正常间距中位数 × 3），两条路径行为一致。
        {
            const int n = 40;
            std::vector<double> xs(n), ys(n);
            for (int i = 0; i < n; ++i)
            {
                xs[i] = (i < 20) ? (0.08 * i) : (3.52 + 0.08 * (i - 20));
                ys[i] = 0.5 * std::sin(0.7 * i); // 全部有效，无 NaN
            }
            nextpilot::plotscope::fastplot::ArrayData data(xs.data(), ys.data(), n);

            nextpilot::plotscope::fastplot::PlotWidget plot;
            plot.resize(800, 400);
            plot.addCurve(nextpilot::plotscope::fastplot::Curve::fromData(&data, ink, 1.4));
            plot.setXRange(0.0, 4.0);
            plot.setYRange(-1.0, 1.0);

            const QImage img = plot.grabImage();
            const auto &vp = plot.viewport();
            // 缺测区间 (1.6, 3.4) 内不允许任何曲线像素
            const int hits = inkPixelsBetween(img, vp.xToPixel(1.6), vp.xToPixel(3.4),
                                              vp.plotRect().top(), vp.plotRect().bottom());
            CHECK(hits == 0);
            // 两侧曲线本体还在
            const int bodyL = inkPixelsBetween(img, vp.xToPixel(0.1), vp.xToPixel(1.4),
                                               vp.plotRect().top(), vp.plotRect().bottom());
            const int bodyR = inkPixelsBetween(img, vp.xToPixel(3.6), vp.xToPixel(3.9),
                                               vp.plotRect().top(), vp.plotRect().bottom());
            CHECK(bodyL > 0 && bodyR > 0);
        }

        fprintf(stdout, "[ok] NaN gap：全null桶/空桶/稀疏NaN/稀疏静默缺测 全部断线，正常数据无假 gap\n");
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
        nextpilot::plotscope::fastplot::ArrayData data(xs.data(), ys.data(), n, 21);

        nextpilot::plotscope::fastplot::PlotWidget plot;
        plot.resize(800, 400);
        plot.addCurve(nextpilot::plotscope::fastplot::Curve::fromData(&data, QColor(0x1F, 0x6F, 0xC0), 1.4));
        plot.setXRange(0.0, 3.0);
        plot.setYRange(-1.2, 1.2);

        nextpilot::plotscope::fastplot::Zoomer zoomer(&plot);
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
        QVector<nextpilot::plotscope::fastplot::LodPoint> left;
        QVector<nextpilot::plotscope::fastplot::LodPoint> right;
        for (int i = 0; i < 400; ++i)
        {
            const double x = i * 0.016;
            left.push_back(nextpilot::plotscope::fastplot::LodPoint{x, 0.5 * std::sin(x), 0.55 * std::sin(x) + 0.02});
            right.push_back(nextpilot::plotscope::fastplot::LodPoint{x, 199.5, 200.5});
        }

        const QColor leftInk(0x1F, 0x6F, 0xC0);
        const QColor rightInk(0xE0, 0x6C, 0x00);

        nextpilot::plotscope::fastplot::PlotWidget plot;
        plot.resize(800, 400);
        plot.setXRange(0.0, 6.4);
        plot.setYRange(-1.0, 1.0);
        plot.setSecondAxis(true);
        plot.setYRange2(190.0, 210.0);

        auto &c1 = plot.addCurve(nextpilot::plotscope::fastplot::Curve::fromLod(left, leftInk, 1.0));
        c1.setName(QStringLiteral("roll"));
        auto &c2 = plot.addCurve(nextpilot::plotscope::fastplot::Curve::fromLod(right, rightInk, 1.0));
        c2.setName(QStringLiteral("alt"));
        c2.setYAxis(nextpilot::plotscope::fastplot::AxisSide::Right);

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
        nextpilot::plotscope::fastplot::PlotWidget restored;
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
            nextpilot::plotscope::fastplot::BarChart bar;
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
            nextpilot::plotscope::fastplot::ArrayData data(xs.data(), ys.data(), n, 41);
            nextpilot::plotscope::fastplot::Histogram hist;
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
            nextpilot::plotscope::fastplot::ArrayData data(xs.data(), ys.data(), n, 43);
            nextpilot::plotscope::fastplot::SpectrumPlot spec;
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

    // 深缩放下限：连续极端 zoom 不得让 X 视口塌缩（对标 PJ4 ULP 下限 + nudge）。
    {
        nextpilot::plotscope::fastplot::Axes plot;
        plot.resize(400, 300);
        plot.setXRange(0.0, 1.0);
        nextpilot::plotscope::fastplot::Zoomer zoomer(&plot);
        const QPointF anchor(plot.plotRect().center());
        for (int i = 0; i < 40; ++i)
            zoomer.zoomBy(1e-6, anchor); // 每次把窗口缩到百万分之一
        const auto &vp = plot.viewport();
        const double w = vp.xUpper() - vp.xLower();
        CHECK(w > 0.0);                 // 不塌缩为 0
        CHECK(w >= 1e-9 * 0.99);        // 不低于 1ns 语义下限（容 double 表示误差）
        CHECK(std::isfinite(vp.xLower()) && std::isfinite(vp.xUpper()));
        fprintf(stdout, "[ok] 深缩放下限：40 次极端 zoom 后 X 宽度仍 >=1ns（%.3g）\n", w);
    }

    // 8) 第1仗脏调度（P-2 渲染侧）：稳态叠加层缓存，游标移动不触发重建。
    {
        using namespace nextpilot::plotscope::fastplot;
        PlotWidget plot;
        plot.resize(800, 400);

        // 先给一个数据范围，marker 竖线才落在可视区
        std::vector<double> xs{0.0, 1.0}, ys{0.0, 1.0};
        ArrayData data(xs.data(), ys.data(), 2, 1);
        plot.addCurve(Curve::fromData(&data, QColor(0x5b, 0xc0, 0xf0), 1.4));
        plot.setXRange(0.0, 1.0);
        plot.setYRange(0.0, 1.0);

        // 首次渲染（无 marker）：稳态缓存建立，记录基线像素
        const QImage before = plot.grabImage();
        const int base = plot.overlayCacheRebuilds();
        CHECK(base >= 1);

        // 稳态层内容变化 → 标脏 → 下次渲染重建
        QVector<MarkerMark> marks;
        MarkerMark m;
        m.tStart = 0.5;
        m.tEnd = 0.5;
        m.color = QColor(0xff, 0x00, 0x00); // 醒目红色竖线
        marks.append(m);
        plot.setMarkers(marks);
        const QImage after = plot.grabImage();
        const int afterMarkers = plot.overlayCacheRebuilds();
        CHECK(afterMarkers == base + 1);

        // 渲染语义断言：稳态层缓存被正确贴出——marker 红色竖线必须出现在图里。
        // 若 drawPixmap 漏贴，层会"消失"而 rebuild 计数仍对，此断言兜住该风险。
        int redPixels = 0;
        for (int y = 0; y < after.height(); ++y)
            for (int x = 0; x < after.width(); ++x)
                if (after.pixelColor(x, y).red() > 200
                    && after.pixelColor(x, y).green() < 80
                    && after.pixelColor(x, y).blue() < 80)
                    ++redPixels;
        CHECK(redPixels > 10);
        // 基线图（无 marker）不应有这么多红像素
        int redBefore = 0;
        for (int y = 0; y < before.height(); ++y)
            for (int x = 0; x < before.width(); ++x)
                if (before.pixelColor(x, y).red() > 200
                    && before.pixelColor(x, y).green() < 80
                    && before.pixelColor(x, y).blue() < 80)
                    ++redBefore;
        CHECK(redPixels > redBefore + 10);

        // 游标连续移动：只走实时层，稳态层不重建（核心断言 P-2）
        for (int i = 0; i < 20; ++i)
        {
            plot.setCursorReadout(0.1 + i * 0.01, true, false,
                                  QStringLiteral("t"), QVector<ReadoutRow>{});
            plot.grabImage();
        }
        CHECK(plot.overlayCacheRebuilds() == afterMarkers);

        // 视口变化：稳态层按世界坐标绘制，缓存必须失效重建
        plot.setXRange(0.0, 0.5);
        plot.grabImage();
        CHECK(plot.overlayCacheRebuilds() == afterMarkers + 1);

        fprintf(stdout, "[ok] 脏调度：20 次游标移动稳态层重建=0，稳态/视口变化正确重建\n");
    }

    // 9) 第3仗 3a：图例四角 + 显隐 set 版（单一出口地基）
    {
        using namespace nextpilot::plotscope::fastplot;

        // --- 四角几何：Legend 直接 paint 到离屏图，blockRect 必须落到对应角 ---
        Legend legend;
        legend.setVisible(true);
        QImage img(400, 300, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::white);
        Legend::PaintInput in;
        in.plotRect = QRectF(0, 0, 400, 300);
        in.baseFont = QFont();
        in.labelColor = QColor(0x20, 0x20, 0x20);
        in.windowColor = QColor(0xff, 0xff, 0xff);
        Legend::Item it;
        it.curveIndex = 0;
        it.name = QStringLiteral("roll");
        it.ink = QColor(0x5b, 0xc0, 0xf0);
        it.visible = true;
        in.items.append(it);
        CHECK(legend.corner() == Legend::Corner::TopRight); // 默认右上（历史行为）
        auto paintAt = [&](Legend::Corner c) {
            legend.setCorner(c);
            QPainter p(&img);
            legend.paint(p, in);
            p.end();
            return legend.blockRect();
        };
        const QRectF rTR = paintAt(Legend::Corner::TopRight);
        CHECK(rTR.left() > 200 && rTR.top() >= 0 && rTR.top() < 60);
        const QRectF rTL = paintAt(Legend::Corner::TopLeft);
        CHECK(rTL.left() >= 0 && rTL.left() < 60 && rTL.top() < 60);
        const QRectF rBL = paintAt(Legend::Corner::BottomLeft);
        CHECK(rBL.left() < 60 && rBL.bottom() > 240);
        const QRectF rBR = paintAt(Legend::Corner::BottomRight);
        CHECK(rBR.left() > 200 && rBR.bottom() > 240);
        fprintf(stdout, "[ok] 图例四角：blockRect 随 corner 贴右上/左上/左下/右下\n");

        // --- 四角随 saveState/restoreState round-trip；旧存档缺键回落默认右上 ---
        PlotWidget cornerPlot;
        cornerPlot.resize(400, 300);
        cornerPlot.setLegendVisible(true);
        cornerPlot.setLegendCorner(Legend::Corner::BottomLeft);
        const QJsonObject cornerState = cornerPlot.saveState();
        PlotWidget cornerRestored;
        cornerRestored.resize(400, 300);
        CHECK(cornerRestored.restoreState(cornerState));
        CHECK(cornerRestored.legendCorner() == Legend::Corner::BottomLeft);
        // 缺键（第1仗之前的旧存档）：保持默认右上，不误复原
        QJsonObject legacyState = cornerState;
        legacyState.remove(QStringLiteral("legendCorner"));
        PlotWidget legacyRestored;
        legacyRestored.resize(400, 300);
        legacyRestored.setLegendCorner(Legend::Corner::TopLeft);
        CHECK(legacyRestored.restoreState(legacyState));
        CHECK(legacyRestored.legendCorner() == Legend::Corner::TopRight);
        fprintf(stdout, "[ok] 图例四角：save/restore round-trip，缺键回落默认右上\n");

        // --- 显隐 set 版：setLegendItemVisible 与 toggle 发同一信号；同值重复置不发 ---
        PlotWidget plot;
        plot.resize(400, 300);
        std::vector<double> xs{0.0, 1.0}, ys{0.0, 1.0};
        ArrayData data(xs.data(), ys.data(), 2, 1);
        plot.addCurve(Line::fromData(&data, QColor(0x5b, 0xc0, 0xf0), 1.4)).setName(QStringLiteral("roll"));
        int toggles = 0, lastIdx = -1;
        bool lastVis = false;
        QObject::connect(&plot, &PlotWidget::legendItemToggled,
                         [&](int idx, bool vis) { ++toggles; lastIdx = idx; lastVis = vis; });
        plot.setLegendItemVisible(0, false);
        CHECK(!plot.legendItemVisible(0));
        CHECK(toggles == 1 && lastIdx == 0 && lastVis == false);
        plot.setLegendItemVisible(0, false); // 同值重复置：不发信号
        CHECK(toggles == 1);
        plot.setLegendItemVisible(0, true);
        CHECK(plot.legendItemVisible(0));
        CHECK(toggles == 2 && lastVis == true);
        plot.toggleLegendItem(0); // toggle 与 set 版同信号（AxesView 单一出口收敛依赖）
        CHECK(!plot.legendItemVisible(0));
        CHECK(toggles == 3 && lastIdx == 0 && lastVis == false);
        fprintf(stdout, "[ok] 显隐 set 版：set/toggle 同信号、同值静默、索引与值正确\n");
    }

    // 10) 第3仗 3c：X/Y 键单轴缩放 + 参考线 + 读数框跟随
    {
        using namespace nextpilot::plotscope::fastplot;

        // --- E-3：应用级 X/Y 键单轴缩放（Key_X 按住 = 滚轮只缩 X 轴）---
        {
            PlotWidget plot;
            plot.resize(400, 300);
            plot.setXRange(0.0, 1.0);
            plot.setYRange(0.0, 1.0);
            Zoomer zoomer(&plot);
            const QPointF center(plot.plotRect().center());

            auto sendKey = [&plot](QEvent::Type type, Qt::Key key) {
                QKeyEvent k(type, key, Qt::NoModifier);
                // 生产路径：按键经事件队列 → QApplication::notify → 应用级过滤器。
                QCoreApplication::postEvent(&plot,
                                            new QKeyEvent(type, key, Qt::NoModifier));
                QCoreApplication::processEvents();
            };
            auto sendWheel = [&plot, &center](int degrees) {
                QWheelEvent wheel(center, QPointF(), QPoint(), QPoint(0, degrees),
                                  Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                QCoreApplication::sendEvent(&plot, &wheel);
            };

            // 探针：确认应用级过滤器链真的会被按键事件触发
            struct Probe : QObject
            {
                int hits = 0;
                bool eventFilter(QObject *, QEvent *e) override
                {
                    if (e->type() == QEvent::KeyPress)
                        ++hits;
                    return false;
                }
            } probe;
            QCoreApplication::instance()->installEventFilter(&probe);
            sendKey(QEvent::KeyPress, Qt::Key_X);
            fprintf(stderr, "[dbg-probe] hits=%d\n", probe.hits);
            QCoreApplication::instance()->removeEventFilter(&probe);
            sendWheel(120); // 放大
            const double xSpanX = plot.viewport().xUpper() - plot.viewport().xLower();
            const double ySpanX = plot.viewport().yUpper() - plot.viewport().yLower();
            fprintf(stderr, "[dbg] xSpan=%.12g ySpan=%.12g\n", xSpanX, ySpanX);
            CHECK(xSpanX < 1.0);               // X 轴缩了
            CHECK(std::abs(ySpanX - 1.0) < 1e-9); // Y 轴没动

            sendKey(QEvent::KeyRelease, Qt::Key_X);
            sendKey(QEvent::KeyPress, Qt::Key_Y);
            sendWheel(-120); // 缩小
            const double xSpanY = plot.viewport().xUpper() - plot.viewport().xLower();
            const double ySpanY = plot.viewport().yUpper() - plot.viewport().yLower();
            CHECK(std::abs(xSpanY - xSpanX) < 1e-12);  // X 轴没动
            CHECK(ySpanY > ySpanX);                    // Y 轴缩放生效
            sendKey(QEvent::KeyRelease, Qt::Key_Y);
            // 两键释放后滚轮恢复双轴
            sendWheel(120);
            CHECK(plot.viewport().xUpper() - plot.viewport().xLower() < xSpanY);
            CHECK(plot.viewport().yUpper() - plot.viewport().yLower() < ySpanY);
            fprintf(stdout, "[ok] X/Y 键单轴缩放：应用级键监听优先于修饰键，释放回落双轴\n");
        }

        // --- F-2：参考线（相对时间轴值）随状态 round-trip ---
        {
            PlotWidget plot;
            plot.resize(400, 300);
            plot.setXRange(0.0, 1.0);
            CHECK(!plot.hasReference());
            plot.setReferenceTime(0.42);
            CHECK(plot.hasReference() && plot.referenceTime() == 0.42);
            const QImage withRef = plot.grabImage();
            plot.clearReference();
            CHECK(!plot.hasReference());
            // 存档 round-trip：referenceT 随视图状态持久化；-1 表示清除
            plot.setReferenceTime(0.42);
            const QJsonObject refState = plot.saveState();
            CHECK(refState.contains(QStringLiteral("referenceT")));
            PlotWidget refRestored;
            refRestored.resize(400, 300);
            CHECK(refRestored.restoreState(refState));
            CHECK(refRestored.hasReference()
                  && std::abs(refRestored.referenceTime() - 0.42) < 1e-12);
            QJsonObject clearedState = refState;
            clearedState.insert(QStringLiteral("referenceT"), -1.0);
            CHECK(refRestored.restoreState(clearedState));
            CHECK(!refRestored.hasReference());
            // 旧存档缺键：保持现状（不误清）
            QJsonObject legacy = refState;
            legacy.remove(QStringLiteral("referenceT"));
            PlotWidget legacyRef;
            legacyRef.resize(400, 300);
            legacyRef.setReferenceTime(0.9);
            CHECK(legacyRef.restoreState(legacy));
            CHECK(legacyRef.hasReference() && legacyRef.referenceTime() == 0.9);
            fprintf(stdout, "[ok] 参考线：存相对轴值 round-trip，缺键保持现状\n");
        }

        // --- E-4：读数框纵向跟随（clamp + 大跳 snap + 限速）---
        {
            PlotWidget plot;
            plot.resize(400, 300);
            std::vector<double> xs{0.0, 1.0}, ys{0.0, 1.0};
            ArrayData data(xs.data(), ys.data(), 2, 1);
            plot.addCurve(Line::fromData(&data, QColor(0x5b, 0xc0, 0xf0), 1.4));
            plot.setXRange(0.0, 1.0);
            plot.setYRange(0.0, 1.0);
            plot.setCursorReadout(0.5, true, false, QStringLiteral("t"),
                                  QVector<ReadoutRow>{{QStringLiteral("roll"),
                                                       QStringLiteral("1.0"),
                                                       QColor(0x5b, 0xc0, 0xf0)}});
            // 锚点在顶部：boxAnchorY 钳制，框整体不出画布
            plot.setCursorPixelY(2.0);
            plot.grabImage();
            const QRectF topBox = plot.cursorReadoutBoxForTest();
            CHECK(topBox.top() >= 0.0); // 画布坐标从 0 起：钳后不可能为负
            // 大跳（>20px）直接 snap：框中心贴合锚点（锚点在画布边缘时钳制到界内）
            const QRectF pr10 = plot.plotRect();
            plot.setCursorPixelY(250.0);
            plot.grabImage();
            const QRectF midBox = plot.cursorReadoutBoxForTest();
            const double targetCenter =
                qBound(pr10.top() + 4.0 + midBox.height() / 2.0, 250.0,
                       pr10.bottom() - 4.0 - midBox.height() / 2.0);
            CHECK(std::abs(midBox.center().y() - targetCenter) <= 4.0 + 1e-6);
            // 小步（限速）：同帧内移动量被 200px/s * dt 限制（dt≈0 → 基本不动）
            plot.setCursorPixelY(250.0 + 10.0);
            plot.grabImage();
            const QRectF after = plot.cursorReadoutBoxForTest();
            CHECK(std::abs(after.top() - midBox.top()) < 10.0); // 未瞬移
            fprintf(stdout, "[ok] 读数框跟随：大跳 snap、限速滑动、出界钳制\n");
        }
    }

    fprintf(stdout, "[ok] fastplot P1~P4 全部通过\n");
    return 0;
}
