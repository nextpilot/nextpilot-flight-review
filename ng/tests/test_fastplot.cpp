// fastplot P1 契约测试：视口映射、刻度划分、布局边距、图层缓存渲染。
//
// 不引入 QtTest —— 与仓库其它测试一致，用 assert + 打印。
#include <fastplot/Axis.h>
#include <fastplot/PlotWidget.h>
#include <fastplot/ScaleDiv.h>
#include <fastplot/Viewport.h>

#include <QApplication>
#include <QDir>
#include <QImage>

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{

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

        assert(near(vp.xToPixel(0.0), 10.0));
        assert(near(vp.xToPixel(1.0), 110.0));
        // y 向上增大：上限在顶边，下限在底边
        assert(near(vp.yToPixel(1.0), 10.0));
        assert(near(vp.yToPixel(0.0), 60.0));
        // 往返一致
        assert(near(vp.pixelToX(vp.xToPixel(0.37)), 0.37));
        assert(near(vp.pixelToY(vp.yToPixel(0.42)), 0.42));

        // lo == hi 自动展开，不除零
        fastplot::Viewport degenerate;
        degenerate.setPlotRect(QRectF(0, 0, 10, 10));
        degenerate.setXRange(5.0, 5.0);
        assert(degenerate.xUpper() > degenerate.xLower());
        assert(std::isfinite(degenerate.xToPixel(5.0)));
        fprintf(stdout, "[ok] Viewport：四角映射精确 + 退化范围自动展开\n");
    }

    // 2) 刻度划分：步长是"好看数"，标签无浮点尾巴
    {
        const fastplot::ScaleDiv div = fastplot::ScaleDiv::linear(0.0, 2.0, 8);
        assert(div.majorStep > 0.0);
        // 0~2 分 8 段 → 步长应落在 0.2 / 0.25 这类"好看数"
        const bool niceStep = near(div.majorStep, 0.2) || near(div.majorStep, 0.25)
            || near(div.majorStep, 0.5);
        assert(niceStep);
        assert(div.lower <= 0.0 + 1e-12);
        assert(div.upper >= 2.0 - 1e-12);

        int majors = 0;
        for (const fastplot::Tick &t : div.ticks)
        {
            if (!t.major)
                continue;
            ++majors;
            assert(!t.label.isEmpty());
            assert(!t.label.contains(QStringLiteral("00000000")));
        }
        assert(majors >= 5);

        // 标签格式：0.25 不带多余 0，-0 归一成 0
        assert(fastplot::ScaleDiv::formatValue(0.25, 0.25) == QStringLiteral("0.25"));
        assert(fastplot::ScaleDiv::formatValue(-0.0, 0.25) == QStringLiteral("0"));
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
        assert(pr.width() > 600 && pr.height() > 300);
        assert(pr.left() >= 24);            // 左轴标签占位
        assert(pr.bottom() <= 400 - 16);    // 下轴标签占位
        assert(plot.axis(fastplot::AxisSide::Bottom).scaleDiv().upper >= 2.0);
        assert(plot.axis(fastplot::AxisSide::Left).scaleDiv().lower <= -1.0);
        // 右轴默认隐藏 → 右边距应为 0
        assert(pr.right() >= 800 - 24);

        // 打开右轴后，右边距增大
        const double widthBefore = plot.plotRect().width();
        plot.axis(fastplot::AxisSide::Right).setVisible(true);
        plot.replot();
        assert(plot.plotRect().width() < widthBefore);
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
        assert(img.size() == QSize(800, 400));

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
        assert(hit >= 3);

        // 导出 PNG，供肉眼验收（ctest 的工作目录是构建目录）
        const QString path = QDir::current().filePath(QStringLiteral("fastplot_p1.png"));
        assert(img.save(path));
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
        assert(!pts.isEmpty());
        assert(pts.size() <= 700);
        // 峰值必须活下来：包络保留列内 min/max，不会被"平均"掉
        double lo = 0.0, hi = 0.0;
        for (const fastplot::LodPoint &pt : pts)
        {
            assert(pt.min <= pt.max);
            lo = std::min(lo, pt.min);
            hi = std::max(hi, pt.max);
        }
        assert(hi >= 0.99 && lo <= -0.99);

        // 同一视口再画一次：必须命中缓存，不重扫
        const int rebuildsBefore = cache.rebuilds();
        cache.points(data, vp);
        assert(cache.rebuilds() == rebuildsBefore);

        // 视口变了就得重算
        vp.setXRange(0.0, 1.5);
        cache.points(data, vp);
        assert(cache.rebuilds() == rebuildsBefore + 1);
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
        assert(plot.autoScale());

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
        assert(curvePixels > 200);

        const QString path = QDir::current().filePath(QStringLiteral("fastplot_p2.png"));
        assert(img.save(path));
        fprintf(stdout, "[ok] 曲线渲染：%d 个曲线像素，PNG 已写出 %s\n", curvePixels,
                path.toUtf8().constData());
    }

    fprintf(stdout, "[ok] fastplot P1+P2 全部通过\n");
    return 0;
}
