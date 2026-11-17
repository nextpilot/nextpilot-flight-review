#include "PlottingCanvas.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QWidget>
#include <QVector>
#include <QCommandLineParser>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#ifdef HAVE_QCUSTOMPLOT
#include "qcustomplot.h"
#endif
#ifdef HAVE_QWT
#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_scale_engine.h>
#endif
#ifdef HAVE_QTCHARTS
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#endif
#ifdef HAVE_JKQT
#include <jkqtplotter.h>
#include <jkqtplotter/graphs/jkqtplines.h>
#endif

struct Dataset
{
    QVector<double> t;
    QVector<double> v;
};

static QVector<Dataset> makeData(int curves, int points, double duration, bool predecimate=false, int columns=1280)
{
    QVector<Dataset> out(curves);
    for (int c = 0; c < curves; ++c)
    {
        Dataset &d = out[c];
        d.t.resize(points);
        d.v.resize(points);
        for (int i = 0; i < points; ++i)
        {
            const double t = duration * i / double(points - 1);
            d.t[i] = t;
            d.v[i] = std::sin(t * (0.4 + 0.03 * c) + c) +
                     0.15 * std::sin(t * (7.0 + c)) + 0.02 * c;
        }
    }
    if (predecimate)
    {
        for (int c = 0; c < curves; ++c)
        {
            Dataset &src = out[c];
            Dataset dst;
            const int n = src.t.size();
            const int buckets = std::max(1, columns);
            dst.t.reserve(buckets * 2);
            dst.v.reserve(buckets * 2);
            for (int b = 0; b < buckets; ++b)
            {
                int i0 = int(qint64(b) * n / buckets);
                int i1 = int(qint64(b + 1) * n / buckets);
                i0 = std::clamp(i0, 0, n - 1);
                i1 = std::clamp(std::max(i1, i0 + 1), 1, n);
                double vmin = src.v[i0], vmax = src.v[i0];
                for (int i = i0 + 1; i < i1; ++i)
                {
                    vmin = std::min(vmin, src.v[i]);
                    vmax = std::max(vmax, src.v[i]);
                }
                const double t0 = src.t[i0];
                const double t1 = src.t[i1 - 1];
                dst.t.append(t0);
                dst.v.append(vmin);
                dst.t.append(t1);
                dst.v.append(vmax);
            }
            src = dst;
        }
    }
    return out;
}

struct Backend
{
    QWidget *widget = nullptr;
    virtual ~Backend() = default;
    virtual void redraw() {}
    virtual QString note() const { return {}; }
};

class SelfBackend : public Backend
{
public:
    explicit SelfBackend(const QVector<Dataset> &data, double duration)
    {
        auto *canvas = new PlottingCanvas;
        for (int i = 0; i < data.size(); ++i)
            canvas->addGeneratedSeries(QStringLiteral("curve %1").arg(i), data[i].t, data[i].v);
        canvas->setTimeRange(0.0, duration);
        canvas->setLegendVisible(false);
        widget = canvas;
    }
};

#ifdef HAVE_QCUSTOMPLOT
class QCustomBackend : public Backend
{
public:
    QCustomBackend(const QVector<Dataset> &data, double duration, bool adaptive)
    {
        auto *plot = new QCustomPlot;
        for (int i = 0; i < data.size(); ++i)
        {
            auto *g = plot->addGraph();
            g->setData(data[i].t, data[i].v, true);
            g->setPen(QPen(QColor::fromHsv((i * 37) % 360, 180, 180), 1));
            g->setAdaptiveSampling(adaptive);
        }
        plot->xAxis->setRange(0, duration);
        plot->yAxis->setRange(-1.4, 1.4);
        plot->legend->setVisible(false);
        widget = plot;
    }
    void redraw() override { static_cast<QCustomPlot *>(widget)->replot(); }
    QString note() const override { return QStringLiteral("adaptiveSampling"); }
};
#endif

#ifdef HAVE_QWT
class QwtBackend : public Backend
{
public:
    QwtBackend(const QVector<Dataset> &data, double duration)
    {
        auto *plot = new QwtPlot;
        for (int i = 0; i < data.size(); ++i)
        {
            auto *c = new QwtPlotCurve(QStringLiteral("curve %1").arg(i));
            c->setSamples(data[i].t, data[i].v);
            c->setPen(QPen(QColor::fromHsv((i * 37) % 360, 180, 180), 1));
            c->attach(plot);
        }
        plot->setAxisScale(QwtAxis::XBottom, 0, duration);
        plot->setAxisScale(QwtAxis::YLeft, -1.4, 1.4);
        plot->replot();
        widget = plot;
    }
    void redraw() override { static_cast<QwtPlot *>(widget)->replot(); }
};
#endif

#ifdef HAVE_QTCHARTS
class ChartsBackend : public Backend
{
public:
    ChartsBackend(const QVector<Dataset> &data, double duration)
    {
        auto *chart = new QChart;
        chart->legend()->hide();
        chart->setAnimationOptions(QChart::NoAnimation);
        for (int i = 0; i < data.size(); ++i)
        {
            auto *s = new QLineSeries;
            QVector<QPointF> pts;
            pts.reserve(data[i].t.size());
            for (int k = 0; k < data[i].t.size(); ++k)
                pts.append(QPointF(data[i].t[k], data[i].v[k]));
            s->replace(pts);
            s->setPen(QPen(QColor::fromHsv((i * 37) % 360, 180, 180), 1));
            chart->addSeries(s);
        }
        chart->createDefaultAxes();
        chart->axes(Qt::Horizontal).first()->setRange(0, duration);
        chart->axes(Qt::Vertical).first()->setRange(-1.4, 1.4);
        auto *view = new QChartView(chart);
        view->setRenderHint(QPainter::Antialiasing, true);
        widget = view;
    }
};
#endif

#ifdef HAVE_JKQT
class JkqtBackend : public Backend
{
public:
    JkqtBackend(const QVector<Dataset> &data, double duration, bool compress)
    {
        auto *plot = new JKQTPlotter;
        plot->getPlotter()->setUseAntiAliasingForGraphs(false);
        plot->getPlotter()->setUseAntiAliasingForSystem(false);
        for (int i = 0; i < data.size(); ++i)
        {
            const size_t xCol = plot->getDatastore()->addCopiedColumn(data[i].t, QStringLiteral("x%1").arg(i));
            const size_t yCol = plot->getDatastore()->addCopiedColumn(data[i].v, QStringLiteral("y%1").arg(i));
            auto *g = new JKQTPXYLineGraph(plot);
            g->setXColumn(xCol);
            g->setYColumn(yCol);
            g->setSymbolType(JKQTPNoSymbol);
            g->setUseNonvisibleLineCompression(compress);
            g->setColor(QColor::fromHsv((i * 37) % 360, 180, 180));
            plot->addGraph(g);
        }
        plot->setXY(0, duration, -1.4, 1.4);
        widget = plot;
    }
    void redraw() override { static_cast<JKQTPlotter *>(widget)->redrawPlot(); }
};
#endif

struct Metrics
{
    double first = 0;
    double median = 0;
    double mean = 0;
    double p95 = 0;
    double fps = 0;
    double ink = 0;
};

static double inkRatio(const QImage &image)
{
    const QRgb bg = image.pixel(0, 0);
    qint64 diff = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
            if (row[x] != bg)
                ++diff;
    }
    return double(diff) / double(image.width() * image.height());
}

static double frameMs(Backend &b, QImage &image)
{
    QElapsedTimer timer;
    timer.start();
    b.redraw();
    QPainter p(&image);
    image.fill(Qt::white);
    b.widget->render(&p);
    p.end();
    return timer.nsecsElapsed() / 1e6;
}

static Metrics measure(Backend &b, int width, int height, int frames)
{
    b.widget->resize(width, height);
    b.widget->show();
    QApplication::processEvents();
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    Metrics m;
    m.first = frameMs(b, image);
    for (int i = 0; i < 2; ++i)
        frameMs(b, image);
    QVector<double> times;
    times.reserve(frames);
    for (int i = 0; i < frames; ++i)
        times.append(frameMs(b, image));
    std::sort(times.begin(), times.end());
    m.median = times[times.size() / 2];
    m.p95 = times[std::min<int>(int(times.size()) - 1, int(times.size() * 0.95))];
    double sum = 0;
    for (double t : times)
        sum += t;
    m.mean = sum / times.size();
    m.fps = m.median > 0 ? 1000.0 / m.median : 0.0;
    m.ink = inkRatio(image);
    return m;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addOption({QStringLiteral("lib"), QStringLiteral("self|qcustom|qwt|jkqt|charts"),
                      QStringLiteral("library"), QStringLiteral("self")});
    parser.addOption({QStringLiteral("curves"), QStringLiteral("curve count"), QStringLiteral("value"), QStringLiteral("10")});
    parser.addOption({QStringLiteral("points"), QStringLiteral("points per curve"), QStringLiteral("value"), QStringLiteral("100000")});
    parser.addOption({QStringLiteral("frames"), QStringLiteral("measured frames"), QStringLiteral("value"), QStringLiteral("20")});
    parser.addOption({QStringLiteral("width"), QStringLiteral("viewport width"), QStringLiteral("value"), QStringLiteral("1280")});
    parser.addOption({QStringLiteral("height"), QStringLiteral("viewport height"), QStringLiteral("value"), QStringLiteral("720")});
    parser.addOption({QStringLiteral("zoom"), QStringLiteral("visible x fraction (0,1]"), QStringLiteral("value"), QStringLiteral("1")});
    parser.addOption({QStringLiteral("adaptive"), QStringLiteral("qcustom adaptive sampling"), QStringLiteral("value"), QStringLiteral("1")});
    parser.addOption({QStringLiteral("jkqtCompress"), QStringLiteral("JKQtPlotter line compression"), QStringLiteral("value"), QStringLiteral("1")});
    parser.addOption({QStringLiteral("predecimate"), QStringLiteral("feed all backends 2 min/max points per pixel column"), QStringLiteral("value"), QStringLiteral("0")});
    parser.process(app);

    const int curves = parser.value(QStringLiteral("curves")).toInt();
    const int points = parser.value(QStringLiteral("points")).toInt();
    const int frames = std::max(1, parser.value(QStringLiteral("frames")).toInt());
    const int width = parser.value(QStringLiteral("width")).toInt();
    const int height = parser.value(QStringLiteral("height")).toInt();
    const double zoom = std::clamp(parser.value(QStringLiteral("zoom")).toDouble(), 0.001, 1.0);
    const QString lib = parser.value(QStringLiteral("lib"));
    const bool adaptive = parser.value(QStringLiteral("adaptive")) != QStringLiteral("0");
    const double duration = 600.0;
    const bool predecimate = parser.value(QStringLiteral("predecimate")) != QStringLiteral("0");
    const auto data = makeData(curves, points, duration, predecimate, width);

    std::unique_ptr<Backend> backend;
    if (lib == QStringLiteral("self"))
        backend = std::make_unique<SelfBackend>(data, duration * zoom);
#ifdef HAVE_QCUSTOMPLOT
    else if (lib == QStringLiteral("qcustom"))
        backend = std::make_unique<QCustomBackend>(data, duration * zoom, adaptive);
#endif
#ifdef HAVE_QWT
    else if (lib == QStringLiteral("qwt"))
        backend = std::make_unique<QwtBackend>(data, duration * zoom);
#endif
#ifdef HAVE_JKQT
    else if (lib == QStringLiteral("jkqt"))
        backend = std::make_unique<JkqtBackend>(data, duration * zoom, parser.value(QStringLiteral("jkqtCompress")) != QStringLiteral("0"));
#endif
#ifdef HAVE_QTCHARTS
    else if (lib == QStringLiteral("charts"))
        backend = std::make_unique<ChartsBackend>(data, duration * zoom);
#endif
    if (!backend)
    {
        std::fprintf(stderr, "backend not available: %s\n", qPrintable(lib));
        return 2;
    }

    Metrics m = measure(*backend, width, height, frames);
    std::printf("{\"lib\":\"%s\",\"curves\":%d,\"points\":%d,\"frames\":%d,\"width\":%d,"
                "\"height\":%d,\"zoom\":%.4f,\"ink\":%.4f,\"first_ms\":%.3f,\"median_ms\":%.3f,"
                "\"mean_ms\":%.3f,\"p95_ms\":%.3f,\"fps\":%.1f}\n",
                qPrintable(lib), curves, points, frames, width, height, zoom, m.ink,
                m.first, m.median, m.mean, m.p95, m.fps);
    return 0;
}
