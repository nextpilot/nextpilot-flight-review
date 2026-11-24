// fastplot 手动基准（不进 ctest）：追平优化前后的对比数据采集（#IKXD1R 4.4）。
//
// 场景：
//   A) 20 曲线 × 10 万点 pan 拖拽：视口连续平移 30 步，每步一整帧
//      —— 考察"视口变化"路径的整帧成本（脏调度下稳态层必须重建）。
//   B) 游标移动 × 60 步：视口固定，只有游标/读数层变化
//      —— 第1仗脏调度（P-2）的核心收益路径：稳态层缓存命中，
//         每帧只重绘实时层；重建计数 delta 即稳态层重建次数。
//
// 用法：fastplot-bench [曲线数=20] [每曲线点数=100000]
// 计时口径：每步 grabImage()（含 widget 渲染到 QImage 的完整管线）。
// 渲染为软件栅格（QT_QPA_PLATFORM=offscreen），绝对值随 CPU 变化，看相对值。

#include "fastplot/Axes.h"
#include "fastplot/BookmarkOverlay.h"
#include "fastplot/Overlay.h"
#include "fastplot/PlotData.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QImage>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace nextpilot::plotscope::fastplot;

namespace
{
double median(std::vector<double> v)
{
    if (v.empty())
        return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

struct CurveSet
{
    std::vector<std::vector<double>> xs, ys;
    std::vector<ArrayData> datas; // 注意：ArrayData 不持有内存，外部数组必须保活
    QVector<Line> handles;
};
} // namespace

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    const int nCurves = argc > 1 ? std::atoi(argv[1]) : 20;
    const int nPoints = argc > 2 ? std::atoi(argv[2]) : 100000;

    // ---- 场景准备：nCurves 条正弦混合曲线，600 s 时间跨度 ----
    CurveSet set;
    set.xs.resize(nCurves);
    set.ys.resize(nCurves);
    set.datas.reserve(nCurves);
    for (int c = 0; c < nCurves; ++c)
    {
        set.xs[c].resize(nPoints);
        set.ys[c].resize(nPoints);
        const double phase = 0.37 * c;
        const double amp = 0.4 + 0.6 * (c % 5) / 5.0;
        for (int i = 0; i < nPoints; ++i)
        {
            const double t = 600.0 * i / double(nPoints - 1);
            set.xs[c][i] = t;
            set.ys[c][i] = amp * std::sin(2.0 * M_PI * (0.5 + 0.1 * c) * t + phase)
                           + 0.1 * std::sin(23.0 * t + phase);
        }
        set.datas.emplace_back(set.xs[c].data(), set.ys[c].data(), nPoints, 41 + c);
    }

    Axes plot;
    plot.resize(1280, 720);
    for (int c = 0; c < nCurves; ++c)
        plot.addCurve(Line::fromData(&set.datas[size_t(c)],
                                     QColor(0x5b + (17 * c) % 128, 0xc0, 0xf0), 1.4));
    plot.setXRange(0.0, 600.0);
    plot.setYRange(-1.6, 1.6);
    plot.show(); // offscreen 平台可 show：让 repaint() 走真实 paintEvent 路径
    QApplication::processEvents();
    plot.repaint(); // 预热 1 帧（首帧含 LOD 首算）

    // ---- A) pan 拖拽：视口连续平移，稳态层每帧重建 ----
    {
        std::vector<double> ms;
        const double span = 60.0; // 每步平移 60 s
        for (int step = 0; step < 30; ++step)
        {
            const double lo = step * span * 0.2;
            plot.setXRange(lo, lo + 600.0);
            QElapsedTimer t;
            t.start();
            plot.repaint(); // 同步 paintEvent：走脏区域与稳态层缓存的真实路径
            const double e = double(t.nsecsElapsed()) / 1e6;
            ms.push_back(e);
        }
        std::sort(ms.begin(), ms.end());
        fprintf(stdout, "[A] pan %d曲线x%d点 30步：中位 %.1f ms（%.1f fps） 首帧 %.1f ms\n",
                nCurves, nPoints, median(ms), 1000.0 / median(ms), ms.front());
    }

    // ---- B0) 轻稳态负载的游标移动（叠加层为空，纯画曲线+实时层）----
    {
        const int rebuildsBefore = plot.overlayCacheRebuilds();
        std::vector<double> ms;
        for (int step = 0; step < 60; ++step)
        {
            const double x = 600.0 * step / 59.0;
            QVector<ReadoutRow> rows;
            rows.push_back({QStringLiteral("ch0"), QStringLiteral("0.42"), QColor(Qt::white)});
            plot.setCursorReadout(x, true, false, QStringLiteral("t = %1 s").arg(x, 0, 'f', 2),
                                  rows);
            QElapsedTimer t;
            t.start();
            plot.repaint(); // 同步 paintEvent：稳态层命中时只重绘实时层
            const double e = double(t.nsecsElapsed()) / 1e6;
            ms.push_back(e);
        }
        const int rebuildsDelta = plot.overlayCacheRebuilds() - rebuildsBefore;
        std::sort(ms.begin(), ms.end());
        fprintf(stdout, "[B0] 轻稳态游标移动 60步：中位 %.1f ms（%.1f fps） 稳态层重建 delta=%d\n",
                median(ms), 1000.0 / median(ms), rebuildsDelta);
    }

    // ---- B) 重稳态负载游标移动：AB 测量 + 40 书签 + 20 状态带常驻，
    //         游标每帧变、稳态组不变——第1仗脏调度（P-2）的核心收益场景 ----
    {
        plot.setAB(true, 150.0, 420.0, true, QStringLiteral("A/B"), {}, false, 0, 0, 0);
        QVector<BookmarkMark> marks;
        for (int i = 0; i < 40; ++i)
            marks.push_back({QStringLiteral("bm%1").arg(i), 15.0 * i,
                             QStringLiteral("书签%1").arg(i), false});
        plot.setBookmarks(marks);
        plot.repaint(); // 预热：稳态层重建一次

        const int rebuildsBefore2 = plot.overlayCacheRebuilds();
        std::vector<double> ms;
        for (int step = 0; step < 60; ++step)
        {
            const double x = 600.0 * step / 59.0;
            QVector<ReadoutRow> rows;
            rows.push_back({QStringLiteral("ch0"), QStringLiteral("0.42"), QColor(Qt::white)});
            plot.setCursorReadout(x, true, false, QStringLiteral("t = %1 s").arg(x, 0, 'f', 2),
                                  rows);
            QElapsedTimer t;
            t.start();
            plot.repaint();
            const double e = double(t.nsecsElapsed()) / 1e6;
            ms.push_back(e);
        }
        const int rebuildsDelta2 = plot.overlayCacheRebuilds() - rebuildsBefore2;
        std::sort(ms.begin(), ms.end());
        fprintf(stdout, "[B] 重稳态游标移动 60步：中位 %.1f ms（%.1f fps） 稳态层重建 delta=%d\n",
                median(ms), 1000.0 / median(ms), rebuildsDelta2);
        plot.setAB(false, 0, 0, false, QString(), {}, false, 0, 0, 0);
        plot.setBookmarks({});
    }

    return 0;
}
