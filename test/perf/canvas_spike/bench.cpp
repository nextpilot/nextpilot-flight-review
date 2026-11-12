#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <random>
#include <vector>

#include "ps_kernel.h"
#include "plot/CanvasWidget.h"
#include "jkqtplotter/jkqtplotter.h"
#include "jkqtplotter/graphs/jkqtplines.h"

static const int NCH = 20;
static const int NPT = 8000;

static QString makeCsv(const QString &path)
{
    std::mt19937 rng(42);
    std::normal_distribution<double> noise(0.0, 0.35);
    QFile f(path);
    f.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream ts(&f);
    ts << "time_ms";
    for (int c = 0; c < NCH; ++c)
        ts << ",ch" << c << "[u" << c << "]";
    ts << "\n";
    for (int i = 0; i < NPT; ++i)
    {
        ts << (i * 10);
        for (int c = 0; c < NCH; ++c)
        {
            const double t = i / 100.0;
            const double amp = 1.0 + 0.1 * c;
            ts << ',' << QString::number(amp * std::sin(2 * M_PI * (0.2 + 0.03 * c) * t) + noise(rng), 'f', 4);
        }
        ts << "\n";
    }
    return path;
}

static double benchWidget(QWidget *w, int frames, const std::function<void()> &before = {})
{
    w->resize(1500, 900);
    w->ensurePolished();
    QImage img(w->size(), QImage::Format_ARGB32_Premultiplied);
    for (int i = 0; i < 3; ++i)  // 预热：字体、渐变、JKQ 内部缓存
    {
        if (before) before();
        img.fill(Qt::black);
        QPainter p(&img);
        w->render(&p);
    }
    QElapsedTimer t;
    t.start();
    for (int i = 0; i < frames; ++i)
    {
        if (before) before();
        img.fill(Qt::black);
        QPainter p(&img);
        w->render(&p);
    }
    return t.elapsed() / double(frames);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    const QString csv = makeCsv(QStringLiteral("canvas_spike_data20.csv"));

    PsDataset *ds = nullptr;
    char err[256] = {0};
    if (ps_csv_load(csv.toUtf8().constData(), &ds, err, sizeof(err)) != 0)
    {
        qCritical() << "load failed" << err;
        return 1;
    }
    const uint32_t nch = ps_dataset_channel_count(ds);

    // ---- 我们的画布：内核通道 + Rust 侧 LOD ----
    QVector<const PsChannelInfo *> chans;
    for (uint32_t i = 0; i < nch; ++i)
        chans.append(ps_dataset_channel(ds, i));

    CanvasWidget canvas;
    canvas.setTitle(QStringLiteral("spike-ours"));
    canvas.setChannels(chans);
    const double ourMs = benchWidget(&canvas, 30);

    CanvasWidget canvas;
    canvas.setTitle(QStringLiteral("spike-ours"));
    canvas.setChannels(chans);
    const double ourMs = benchWidget(&canvas, 30);

    // Qt 光栅器本身的基线：1500 段折线 × 20 条，AA 描边，画到同一张 1500×900 的图
    {
        QImage img(1500, 900, QImage::Format_ARGB32_Premultiplied);
        QElapsedTimer t; t.start();
        const int frames = 30;
        for (int f = 0; f < frames; ++f)
        {
            img.fill(Qt::black);
            QPainter pp(&img);
            pp.setRenderHint(QPainter::Antialiasing, true);
            for (int c = 0; c < 20; ++c)
            {
                QPainterPath path;
                for (int i = 0; i < 1500; ++i)
                {
                    const double x = i * 1500.0 / 1500.0;
                    const double y = 450 + 300 * std::sin((i + c * 37) * 0.05);
                    if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
                }
                pp.setPen(QPen(QColor::fromHsv(c * 18 % 360, 200, 230), 1.3));
                pp.drawPath(path);
            }
        }
        qInfo().noquote() << QStringLiteral("[qt  ] 20 × 1500 seg AA : %1 ms/frame").arg(t.elapsed() / double(frames), 0, 'f', 2);
    }

    // 光栅基线二：同样的折线，AA 关、线宽 1.0 / 1.3 各测一次
    for (double w : {1.0, 1.3})
    {
        QImage img(1500, 900, QImage::Format_ARGB32_Premultiplied);
        QElapsedTimer t; t.start();
        const int frames = 30;
        for (int f = 0; f < frames; ++f)
        {
            img.fill(Qt::black);
            QPainter pp(&img);
            pp.setRenderHint(QPainter::Antialiasing, false);
            for (int c = 0; c < 20; ++c)
            {
                QPolygonF poly;
                poly.reserve(1500);
                for (int i = 0; i < 1500; ++i)
                    poly.append(QPointF(i, 450 + 300 * std::sin((i + c * 37) * 0.05)));
                pp.setPen(QPen(QColor::fromHsv(c * 18 % 360, 200, 230), w));
                pp.drawPolyline(poly);
            }
        }
        qInfo().noquote() << QStringLiteral("[qt  ] 20 × 1500 seg noAA w=%1 : %2 ms/frame")
                                 .arg(w).arg(t.elapsed() / double(frames), 0, 'f', 2);
    }

    CanvasWidget empty;
    empty.setTitle(QStringLiteral("spike-empty"));
    const double emptyMs = benchWidget(&empty, 30);

    // 单通道版本：区分"每通道固定开销"与"通道数放大"
    CanvasWidget one;
    one.setTitle(QStringLiteral("spike-1ch"));
    one.setChannels({chans[0]});
    const double oneMs = benchWidget(&one, 30);

    // 纯 LOD 成本（不画）：20 通道 × 1500 桶
    {
        QVector<double> buf(1500 * 3);
        QElapsedTimer t; t.start();
        int frames = 30;
        for (int k = 0; k < frames; ++k)
            for (const PsChannelInfo *ch : chans)
                ps_lod_minmax(ch->time, ch->value, ch->point_count, 0.0, 80.0, 1500, buf.data(), 1500);
        qInfo().noquote() << QStringLiteral("[lod ]  20 ch lod only : %1 ms/round").arg(t.elapsed() / double(frames), 0, 'f', 2);
    }
    qInfo().noquote() << QStringLiteral("[ours0] 0 ch : %1 ms/frame").arg(emptyMs, 0, 'f', 2);
    qInfo().noquote() << QStringLiteral("[ours1] 1 ch × %1 pts : %2 ms/frame").arg(NPT).arg(oneMs, 0, 'f', 2);
    qInfo().noquote() << QStringLiteral("[ours]  %1 ch × %2 pts : %3 ms/frame")
                             .arg(nch).arg(NPT).arg(ourMs, 0, 'f', 2);

    // ---- JKQTPlotter：原始点，交给库自己画 ----
    JKQTPlotter plot;
    plot.setToolbarEnabled(false);
    plot.getPlotter()->setUseAntiAliasingForGraphs(true);
    plot.getPlotter()->setPlotLabel(QStringLiteral("spike-jkqt"));
    for (uint32_t i = 0; i < nch; ++i)
    {
        const PsChannelInfo *ch = ps_dataset_channel(ds, i);
        const size_t cx = plot.getDatastore()->addCopiedColumn(ch->time, ch->point_count, QStringLiteral("x%1").arg(i));
        const size_t cy = plot.getDatastore()->addCopiedColumn(ch->value, ch->point_count, ch->name);
        auto *g = new JKQTPXYLineGraph(&plot);
        g->setXColumn(cx);
        g->setYColumn(cy);
        g->setColor(QColor::fromHsv(int(360.0 * i / nch), 200, 230));
        plot.addGraph(g);
    }
    const double jkMs = benchWidget(&plot, 30, [&plot] { plot.redrawPlot(); });
    plot.getPlotter()->setUseAntiAliasingForGraphs(false);
    const double jkMsNoAA = benchWidget(&plot, 30, [&plot] { plot.redrawPlot(); });
    qInfo().noquote() << QStringLiteral("[jkqt-] %1 ch × %2 pts (no AA) : %3 ms/frame")
                             .arg(nch).arg(NPT).arg(jkMsNoAA, 0, 'f', 2);
    qInfo().noquote() << QStringLiteral("[jkqt]  %1 ch × %2 pts : %3 ms/frame")
                             .arg(nch).arg(NPT).arg(jkMs, 0, 'f', 2);

    // ---- JKQTPlotter：只喂 LOD 后的点（"库 + 同样的抽稀"）----
    {
        JKQTPlotter plot2;
        plot2.setToolbarEnabled(false);
        plot2.getPlotter()->setUseAntiAliasingForGraphs(false);
        for (uint32_t i = 0; i < nch; ++i)
        {
            const PsChannelInfo *ch = ps_dataset_channel(ds, i);
            std::vector<double> bx, by;
            const uint32_t px = 1500;
            std::vector<double> buf(px * 3);
            const uint32_t got = ps_lod_minmax(ch->time, ch->value, ch->point_count, 0.0, 80.0, px, buf.data(), px);
            for (uint32_t k = 0; k < got; ++k)
            {
                bx.push_back(buf[k * 3]);
                by.push_back((buf[k * 3 + 1] + buf[k * 3 + 2]) / 2.0);
            }
            const size_t cx = plot2.getDatastore()->addCopiedColumn(bx.data(), bx.size(), QStringLiteral("x%1").arg(i));
            const size_t cy = plot2.getDatastore()->addCopiedColumn(by.data(), by.size(), ch->name);
            auto *g = new JKQTPXYLineGraph(&plot2);
            g->setXColumn(cx);
            g->setYColumn(cy);
            g->setColor(QColor::fromHsv(int(360.0 * i / nch), 200, 230));
            plot2.addGraph(g);
        }
        const double ms = benchWidget(&plot2, 30, [&plot2] { plot2.redrawPlot(); });
        qInfo().noquote() << QStringLiteral("[jkqtL] %1 ch × 1500 pts(LOD) : %2 ms/frame").arg(nch).arg(ms, 0, 'f', 2);
    }

    canvas.clearChannels();
    ps_dataset_free(ds);
    return 0;
}
