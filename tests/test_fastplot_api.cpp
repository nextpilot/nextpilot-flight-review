// fastplot 术语对齐契约测试：Figure/Axes/Line/Bar/Histogram/Spectrum 与 artist 模型。
//
// 目标：证明新术语不是空壳别名，而是真的对象模型：
//   - Figure 容纳多个 Axes（subplot 分层）
//   - plot()/bar()/hist() 画出来的东西真的出现在像素里
//   - 旧名（PlotWidget/Curve/BarChart/SpectrumPlot）与新名是同一类型
// 硬断言 CHECK：Release 下 -DNDEBUG 不会让它变成空跑。
#include <fastplot/Artist.h>
#include <fastplot/Axes.h>
#include <fastplot/Bar.h>
#include <fastplot/BarChart.h>
#include <fastplot/Curve.h>
#include <fastplot/Figure.h>
#include <fastplot/Histogram.h>
#include <fastplot/Line.h>
#include <fastplot/PlotData.h>
#include <fastplot/PlotWidget.h>
#include <fastplot/SemanticColors.h>
#include <fastplot/Spectrum.h>
#include <fastplot/SpectrumPlot.h>

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QVector>

#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <vector>

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
            std::exit(1);                                                                \
        }                                                                                \
    } while (0)

using namespace nextpilot::plotscope::fastplot;

namespace
{
int inkPixels(const QImage &img, const QColor &target, int tol = 12)
{
    int n = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
        {
            const QColor c = img.pixelColor(x, y);
            if (std::abs(c.red() - target.red()) <= tol &&
                std::abs(c.green() - target.green()) <= tol &&
                std::abs(c.blue() - target.blue()) <= tol)
                ++n;
        }
    return n;
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

    // 1) 旧名就是新名的别名（编译期契约）。
    static_assert(std::is_same_v<Curve, Line>, "Curve 必须是 Line 的别名");
    static_assert(std::is_same_v<PlotWidget, Axes>, "PlotWidget 必须是 Axes 的别名");
    static_assert(std::is_same_v<BarChart, Bar>, "BarChart 必须是 Bar 的别名");
    static_assert(std::is_same_v<SpectrumPlot, Spectrum>, "SpectrumPlot 必须是 Spectrum 的别名");
    static_assert(std::is_base_of_v<Artist, Line>, "Line 必须是 artist");
    static_assert(std::is_base_of_v<Artist, Bar>, "Bar 必须是 artist");
    fprintf(stdout, "[ok] 术语别名 + artist 继承（编译期）\n");

    // 2) Figure 多坐标系：subplot 真正分层，一个 figure 多个 axes。
    {
        Figure fig;
        Axes *a0 = fig.axes();
        CHECK(a0 != nullptr);
        CHECK(fig.axesCount() == 1);
        Axes *top = fig.subplot(2, 1, 0);
        Axes *bottom = fig.subplot(2, 1, 1);
        CHECK(top != nullptr && bottom != nullptr);
        CHECK(top != bottom);
        CHECK(fig.axesCount() == 2);
        // 重复占同一格复用同一个 Axes。
        CHECK(fig.subplot(2, 1, 1) == bottom);
        CHECK(fig.gca() == fig.axes());
        fprintf(stdout, "[ok] Figure/subplot：一个 figure 多个 axes，复用同格\n");
    }

    // 3) axes.plot() 画曲线，真的出现在像素里；grid/xlim/xlabel 生效。
    {
        std::vector<double> x(64), y(64);
        for (int i = 0; i < 64; ++i)
        {
            x[i] = i / 63.0;
            y[i] = 0.5 + 0.4 * std::sin(i * 0.2);
        }
        ArrayData data(x.data(), y.data(), 64, 1);

        Axes ax;
        ax.grid(false);
        ax.xlim(0.0, 1.0);
        ax.ylim(0.0, 1.0);
        ax.xlabel(QStringLiteral("time"));

        const QColor lineInk(0x5b, 0xc0, 0xf0);
        Line &line = ax.plot(Line::fromData(&data, lineInk, 2.0));
        CHECK(line.pointCount() == 64);
        CHECK(ax.curveCount() == 1);
        CHECK(ax.plotRect().isValid());
        const QImage img = render(ax, 480, 320);
        // 曲线用自定义色画出来：像素里必须有接近该色的点。
        CHECK(inkPixels(img, lineInk) > 20);
        fprintf(stdout, "[ok] axes.plot()：曲线真画在坐标系里\n");
    }

    // 4) axes.bar()：artist 挂载到坐标系并绘制，柱子颜色出现在像素里。
    {
        Axes ax;
        ax.grid(false);
        const QColor barInk(0x3d, 0xdc, 0x84);
        Bar &bar = ax.bar({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")},
                          {1.0, 2.0, 1.5}, {barInk, barInk, barInk});
        CHECK(ax.artistCount() == 1);
        bar.setName(QStringLiteral("bars")); // Artist 契约：artist 可有图例名
        CHECK(bar.name() == QStringLiteral("bars"));
        const QImage img = render(ax, 420, 300);
        CHECK(inkPixels(img, barInk, 16) > 100);
        ax.clearArtists();
        CHECK(ax.artistCount() == 0);
        const QImage empty = render(ax, 420, 300);
        CHECK(inkPixels(empty, barInk, 16) < inkPixels(img, barInk, 16));
        fprintf(stdout, "[ok] axes.bar()：柱图作为 artist 挂在坐标系上并可清除\n");
    }

    // 5) axes.hist() / raw addArtist：直方图 artist 真能画。
    {
        std::vector<double> v(200);
        for (int i = 0; i < 200; ++i)
            v[i] = std::sin(i * 0.1);
        ArrayData data(v.data(), v.data(), 200, 7); // x=y（直方图只用 y）
        Axes ax;
        ax.grid(false);
        Histogram &h = ax.hist(&data, 24);
        CHECK(ax.artistCount() == 1);
        const QImage img = render(ax, 420, 300);
        CHECK(inkPixels(img, h.color(), 16) > 50);
        fprintf(stdout, "[ok] axes.hist()：直方图 artist 绘制生效\n");
    }

    // 6) Figure + subplot 总览：上曲线、下柱状，导出作可视证据。
    {
        std::vector<double> x(80), y(80);
        for (int i = 0; i < 80; ++i)
        {
            x[i] = i / 79.0;
            y[i] = 0.5 + 0.3 * std::sin(i * 0.18);
        }
        ArrayData data(x.data(), y.data(), 80, 9);

        Figure fig;
        Axes *top = fig.subplot(2, 1, 0);
        Axes *bottom = fig.subplot(2, 1, 1);
        top->grid(true);
        top->xlabel(QStringLiteral("time [s]"));
        top->ylabel(QStringLiteral("amp"));
        top->plot(Line::fromData(&data, QColor(0x5b, 0xc0, 0xf0), 1.8));
        bottom->grid(false);
        bottom->bar({QStringLiteral("roll"), QStringLiteral("pitch"), QStringLiteral("yaw")},
                    {1.0, 1.7, 1.3},
                    {QColor(0x3d, 0xdc, 0x84), QColor(0x5b, 0xc0, 0xf0), QColor(0xff, 0x6b, 0x6b)});
        const QImage img = render(fig, 720, 520);
        const QString path = QStringLiteral("fastplot_figure_subplot.png");
        CHECK(img.save(path));
        fprintf(stdout, "[ok] Figure/subplot 总览已写出 %s\n", qPrintable(path));
    }

    // 语义色接缝（验收 8）：库不认识 token 表，只按 id 取上层注入的实色。
    // 值对象注入（采纳 !106 8da28583）：库层可独立验证，不需要 QWidget。
    {
        using nextpilot::plotscope::fastplot::SemanticColors;
        const QColor fallback(QStringLiteral("#f2b134"));
        const QColor injected(QStringLiteral("#00ff00"));
        SemanticColors colors;
        // 未注入：取到调用方兜底色（外观与接管前一致）。
        CHECK(colors.color(QStringLiteral("plot.cursor"), fallback) == fallback);
        CHECK(!colors.contains(QStringLiteral("plot.cursor")));
        // 注入后：按 id 命中实色，其它 id 仍取各自兜底。
        colors.set(QStringLiteral("plot.cursor"), injected);
        CHECK(colors.contains(QStringLiteral("plot.cursor")));
        CHECK(colors.color(QStringLiteral("plot.cursor"), fallback) == injected);
        CHECK(colors.color(QStringLiteral("plot.abMeasure"), fallback) == fallback);
        CHECK(colors.count() == 1);
        // clear 后回落兜底（主题卸载/重建路径）。
        colors.clear();
        CHECK(colors.color(QStringLiteral("plot.cursor"), fallback) == fallback);
        CHECK(colors.count() == 0);
        fprintf(stdout, "[ok] 语义色接缝：按 id 注入/兜底/清除均正确\n");
    }

    fprintf(stdout, "[ok] fastplot 术语对齐 API 全部通过\n");
    return 0;
}
