// plot.instrument 意图 + InstrumentView 契约测试。
//
// 验证链路：数据集 -> plot.instrument 意图（按角色取有界序列/末值）-> InstrumentView
// 按 kind 切到对应 fastplot 仪表并出图。与 ng_test_gui 一样在 offscreen 下跑。

#include "App.h"
#include "MainWindow.h"
#include "InstrumentView.h"
#include "PanelFactoryRegistry.h"

#include "DataRepository.h"
#include "Dataset.h"
#include "IngestPipeline.h"
#include "Intent.h"
#include "ModuleHost.h"
#include "ParserRegistry.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace ng::app;
using namespace ng::core;
using namespace ng::gui;
using namespace ng::host;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
            std::exit(1);                                                                \
        }                                                                                \
    } while (0)

#define OK(msg) fprintf(stdout, "[ok] %s\n", msg)

static int buildInstrumentDataset(DataRepository &repo)
{
    Dataset ds = Dataset::create(QStringLiteral("instrument-test"), QStringLiteral("mem"));
    const int n = 600;
    QVector<double> t(n);
    for (int i = 0; i < n; ++i)
        t[i] = i * 0.01;

    QVector<double> roll(n), pitch(n), yaw(n), rpm(n), lon(n), lat(n);
    QVector<double> x(n), y(n), z(n), a(n), b(n), c(n);
    for (int i = 0; i < n; ++i)
    {
        roll[i] = 20.0 * std::sin(i * 0.05);
        pitch[i] = 10.0 * std::cos(i * 0.04);
        yaw[i] = std::fmod(i * 0.7, 360.0);
        rpm[i] = 55.0 + 10.0 * std::sin(i * 0.03);
        lon[i] = 104.06 + 0.02 * std::sin(i * 0.02);
        lat[i] = 30.66 + 0.012 * std::cos(i * 0.015);
        x[i] = std::cos(i * 0.02) * 5.0;
        y[i] = std::sin(i * 0.02) * 5.0;
        z[i] = i * 0.02;
        a[i] = std::sin(i * 0.04);
        b[i] = std::sin(i * 0.04 + 1.0);
        c[i] = std::sin(i * 0.04 + 2.0);
    }

    auto table = [&](const QString &name, const QVector<QVector<double> *> &cols,
                     const QStringList &names) {
        if (ds.addTable(name, t) < 0)
            return false;
        for (int k = 0; k < cols.size(); ++k)
            if (!ds.addF64Column(name, names.at(k), QString(), *cols.at(k)))
                return false;
        return true;
    };

    if (!table(QStringLiteral("attitude"), {&roll, &pitch, &yaw},
               {QStringLiteral("roll"), QStringLiteral("pitch"), QStringLiteral("yaw")}))
        return -1;
    if (!table(QStringLiteral("engine"), {&rpm}, {QStringLiteral("rpm")}))
        return -1;
    if (!table(QStringLiteral("gps"), {&lon, &lat}, {QStringLiteral("lon"), QStringLiteral("lat")}))
        return -1;
    if (!table(QStringLiteral("pos"), {&x, &y, &z},
               {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")}))
        return -1;
    if (!table(QStringLiteral("trace"), {&a, &b, &c},
               {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}))
        return -1;

    ds.refreshSchema();
    return repo.add(ds);
}

// 画面里有多少非背景像素（仪表确实画了东西才算通过）。
static int inkPixels(const QImage &img)
{
    const QColor bg(0x10, 0x16, 0x1c);
    int n = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (img.pixelColor(x, y) != bg)
                ++n;
    return n;
}

static QVariantMap seriesOf(const IntentResult &r, const QString &role)
{
    return r.payload.toMap().value(QStringLiteral("series")).toMap().value(role).toMap();
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    App application;
    MainWindow window(application.context());
    window.build(application.modules());
    window.resize(1280, 800);

    const int dsId = buildInstrumentDataset(application.data());
    CHECK(dsId >= 0);

    InstrumentView view(application.modules());
    view.resize(360, 300);

    // --- 1) 姿态仪：roll/pitch 两个角色 ---
    {
        Intent intent;
        intent.op = QStringLiteral("plot.instrument");
        intent.params[QStringLiteral("dataset")] = dsId;
        intent.params[QStringLiteral("kind")] = QStringLiteral("attitude");
        intent.params[QStringLiteral("roles")] = QVariantMap{
            {QStringLiteral("roll"), QStringLiteral("attitude.roll")},
            {QStringLiteral("pitch"), QStringLiteral("attitude.pitch")},
        };
        const auto result = application.modules().execute(intent);
        CHECK(result);
        const QVariantMap scalars = result.value().payload.toMap().value(QStringLiteral("scalars")).toMap();
        CHECK(scalars.contains(QStringLiteral("roll")));
        CHECK(scalars.contains(QStringLiteral("pitch")));
        CHECK(!seriesOf(result.value(), QStringLiteral("roll")).isEmpty());

        view.renderInstrument(dsId, QStringLiteral("attitude"),
                              intent.params.value(QStringLiteral("roles")).toMap(), {}, {});
        CHECK(view.currentKind() == QStringLiteral("attitude"));
        CHECK(inkPixels(view.grabInstrument()) > 300);
        OK("姿态仪：roll/pitch 角色取数 + 出图");
    }

    // --- 2) 罗盘：yaw 角色 ---
    {
        const QVariantMap roles{{QStringLiteral("yaw"), QStringLiteral("attitude.yaw")}};
        Intent intent;
        intent.op = QStringLiteral("plot.instrument");
        intent.params[QStringLiteral("dataset")] = dsId;
        intent.params[QStringLiteral("kind")] = QStringLiteral("compass");
        intent.params[QStringLiteral("roles")] = roles;
        const auto result = application.modules().execute(intent);
        CHECK(result);
        CHECK(result.value().payload.toMap().value(QStringLiteral("scalars")).toMap()
                  .contains(QStringLiteral("yaw")));

        view.renderInstrument(dsId, QStringLiteral("compass"), roles, {}, {});
        CHECK(view.currentKind() == QStringLiteral("compass"));
        CHECK(inkPixels(view.grabInstrument()) > 200);
        OK("罗盘：yaw 取数 + 出图");
    }

    // --- 3) 表盘：value 角色；量程缺省由数据 min/max 推 ---
    {
        const QVariantMap roles{{QStringLiteral("value"), QStringLiteral("engine.rpm")}};
        Intent intent;
        intent.op = QStringLiteral("plot.instrument");
        intent.params[QStringLiteral("dataset")] = dsId;
        intent.params[QStringLiteral("kind")] = QStringLiteral("gauge");
        intent.params[QStringLiteral("roles")] = roles;
        const auto result = application.modules().execute(intent);
        CHECK(result);
        const QVariantList range = result.value().payload.toMap().value(QStringLiteral("range")).toList();
        CHECK(range.size() == 2);
        CHECK(range.at(1).toDouble() > range.at(0).toDouble());

        view.renderInstrument(dsId, QStringLiteral("gauge"), roles, {}, {});
        CHECK(view.currentKind() == QStringLiteral("gauge"));
        CHECK(inkPixels(view.grabInstrument()) > 200);
        OK("表盘：value 取数 + 数据量程 + 出图");
    }

    // --- 4) 经纬轨迹：lon/lat ---
    {
        const QVariantMap roles{
            {QStringLiteral("lon"), QStringLiteral("gps.lon")},
            {QStringLiteral("lat"), QStringLiteral("gps.lat")},
        };
        Intent intent;
        intent.op = QStringLiteral("plot.instrument");
        intent.params[QStringLiteral("dataset")] = dsId;
        intent.params[QStringLiteral("kind")] = QStringLiteral("track");
        intent.params[QStringLiteral("roles")] = roles;
        const auto result = application.modules().execute(intent);
        CHECK(result);
        CHECK(!seriesOf(result.value(), QStringLiteral("lon")).isEmpty());
        CHECK(!seriesOf(result.value(), QStringLiteral("lat")).isEmpty());

        view.renderInstrument(dsId, QStringLiteral("track"), roles, {}, {});
        CHECK(view.currentKind() == QStringLiteral("track"));
        CHECK(inkPixels(view.grabInstrument()) > 200);
        OK("经纬轨迹：lon/lat 两条序列 + 出图");
    }

    // --- 5) 3D 轨迹：x/y/z ---
    {
        const QVariantMap roles{
            {QStringLiteral("x"), QStringLiteral("pos.x")},
            {QStringLiteral("y"), QStringLiteral("pos.y")},
            {QStringLiteral("z"), QStringLiteral("pos.z")},
        };
        Intent intent;
        intent.op = QStringLiteral("plot.instrument");
        intent.params[QStringLiteral("dataset")] = dsId;
        intent.params[QStringLiteral("kind")] = QStringLiteral("traj3d");
        intent.params[QStringLiteral("roles")] = roles;
        const auto result = application.modules().execute(intent);
        CHECK(result);
        CHECK(!seriesOf(result.value(), QStringLiteral("x")).isEmpty());
        CHECK(!seriesOf(result.value(), QStringLiteral("z")).isEmpty());

        view.renderInstrument(dsId, QStringLiteral("traj3d"), roles, {}, {});
        CHECK(view.currentKind() == QStringLiteral("traj3d"));
        CHECK(inkPixels(view.grabInstrument()) > 100);
        OK("3D 轨迹：x/y/z 序列 + 出图");
    }

    // --- 6) 瀑布：channels 列表 ---
    {
        const QVariantList channels{
            QStringLiteral("trace.a"), QStringLiteral("trace.b"), QStringLiteral("trace.c")};
        Intent intent;
        intent.op = QStringLiteral("plot.instrument");
        intent.params[QStringLiteral("dataset")] = dsId;
        intent.params[QStringLiteral("kind")] = QStringLiteral("waterfall");
        intent.params[QStringLiteral("channels")] = channels;
        const auto result = application.modules().execute(intent);
        CHECK(result);
        const QVariantList out = result.value().payload.toMap().value(QStringLiteral("channels")).toList();
        CHECK(out.size() == 3);

        view.renderInstrument(dsId, QStringLiteral("waterfall"), {}, channels, {});
        CHECK(view.currentKind() == QStringLiteral("waterfall"));
        CHECK(inkPixels(view.grabInstrument()) > 300);
        OK("瀑布图：三条序列错开堆叠 + 出图");
    }

    // --- 7) 错误语义：未知 kind / 缺通道 / 不存在数据集 ---
    {
        Intent badKind;
        badKind.op = QStringLiteral("plot.instrument");
        badKind.params[QStringLiteral("dataset")] = dsId;
        badKind.params[QStringLiteral("kind")] = QStringLiteral("hologram");
        CHECK(!application.modules().execute(badKind));

        Intent missing;
        missing.op = QStringLiteral("plot.instrument");
        missing.params[QStringLiteral("dataset")] = dsId;
        missing.params[QStringLiteral("kind")] = QStringLiteral("gauge");
        missing.params[QStringLiteral("roles")] = QVariantMap{
            {QStringLiteral("value"), QStringLiteral("nope.nope")}};
        CHECK(!application.modules().execute(missing));

        Intent noDataset;
        noDataset.op = QStringLiteral("plot.instrument");
        noDataset.params[QStringLiteral("dataset")] = 9999;
        noDataset.params[QStringLiteral("kind")] = QStringLiteral("gauge");
        CHECK(!application.modules().execute(noDataset));
        OK("错误语义：未知类型 / 缺通道 / 无数据集 均返回失败");
    }

    // --- 8) 面板工厂自注册：plot-instrument 可按 id 取到 ---
    CHECK(PanelFactoryRegistry::types().contains(QStringLiteral("plot-instrument")));
    QWidget *panel = PanelFactoryRegistry::create(
        QStringLiteral("plot-instrument"), application.context(), application.modules(), nullptr);
    CHECK(qobject_cast<InstrumentView *>(panel) != nullptr);
    OK("面板自注册：plot-instrument 进工厂表");

    // --- 9) 真实 ULog fixture：姿态/罗盘/表盘走完整管道出图 ---
    {
        QString path = qgetenv("FASTPLOT_ULG_FIXTURE");
        if (path.isEmpty())
            path = QStringLiteral(
                "/home/user/Doubao/chats/38453274206336514/nextpilot-ng/tests/fixture/"
                "demo_flight.ulg");
        if (QFile::exists(path))
        {
            IngestPipeline pipe(application.data());
            const Result<int> bound = pipe.bindParser(1, ParserBinding{QStringLiteral("ulog"), {}, true});
            CHECK(bound);
            QFile file(path);
            CHECK(file.open(QIODevice::ReadOnly));
            QByteArray chunk;
            while (!file.atEnd())
            {
                chunk = file.read(4096);
                if (chunk.isEmpty())
                    break;
                pipe.feed(1, reinterpret_cast<const uint8_t *>(chunk.constData()), chunk.size());
            }
            file.close();
            pipe.finish(1);
            const int realId = bound.value();

            InstrumentView realView(application.modules());
            realView.resize(360, 300);

            const QVariantMap attitudeRoles{
                {QStringLiteral("roll"), QStringLiteral("attitude.roll")},
                {QStringLiteral("pitch"), QStringLiteral("attitude.pitch")}};
            realView.renderInstrument(realId, QStringLiteral("attitude"), attitudeRoles, {}, {});
            CHECK(inkPixels(realView.grabInstrument()) > 300);
            realView.grabInstrument().save(QDir::currentPath() + "/instrument_real_attitude.png");

            const QVariantMap compassRoles{{QStringLiteral("yaw"), QStringLiteral("attitude.yaw")}};
            realView.renderInstrument(realId, QStringLiteral("compass"), compassRoles, {}, {});
            CHECK(inkPixels(realView.grabInstrument()) > 200);
            realView.grabInstrument().save(QDir::currentPath() + "/instrument_real_compass.png");

            const QVariantMap gaugeRoles{{QStringLiteral("value"), QStringLiteral("engine.rpm")}};
            realView.renderInstrument(realId, QStringLiteral("gauge"), gaugeRoles, {}, {});
            CHECK(inkPixels(realView.grabInstrument()) > 200);
            realView.grabInstrument().save(QDir::currentPath() + "/instrument_real_gauge.png");
            OK("真实 ULog：姿态/罗盘/表盘经摄取管道取数并出图");
        }
        else
        {
            OK("真实 ULog fixture 不存在，跳过");
        }
    }

    fprintf(stdout, "\nplot.instrument 契约测试全部通过\n");
    return 0;
}
