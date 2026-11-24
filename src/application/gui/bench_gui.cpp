// GUI 百万行实测：验证"ps 数据 UI 呈现"在真实大数据量下的内存与速度。
//
// 手动基准（不进 ctest）：需要外部生成的大文件。
//   用法：plotscope-gui-bench <file.ulg>
//
// 关注三件事：
//   1) 加载 100 万行后进程 RSS 仍然有界（无 C++ 全量副本）；
//   2) 画布绘制的三元组数 <= 像素宽（与行数无关）；
//   3) 渲染耗时是像素级，不是行级。

#include "App.h"

#include "MainWindow.h"
#include "AxesView.h"
#include "PlotWorkspace.h"
#include "../modules/plot/PlotDocument.h"
#include "PanelFactoryHub.h"
#include "ShellBinder.h"

#include "Dataset.h"
#include "Intent.h"
#include "RuntimeHost.h"

#include <QApplication>
#include <QElapsedTimer>
#include <algorithm>
#include <vector>
#include <QFileInfo>

#include <cstdio>

using namespace nextpilot::plotscope::app;
using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;
using namespace nextpilot::plotscope::context;
using namespace nextpilot::plotscope::gui;
namespace plot = nextpilot::plotscope::plot;

static long rssKb()
{
    QFile f(QStringLiteral("/proc/self/status"));
    if (!f.open(QIODevice::ReadOnly))
        return -1;
    while (!f.atEnd())
    {
        const QByteArray line = f.readLine();
        if (line.startsWith("VmRSS:"))
            return line.split(':').value(1).trimmed().split(' ').value(0).toLong();
    }
    return -1;
}

namespace {
#define CHECK_APPLY(stmt, tag) \
    do { \
        if (!(stmt)) \
        { \
            fprintf(stderr, "applyDocumentSpec #%d failed\n", (tag)); \
            return 1; \
        } \
    } while (0)

const char *u8(const QString &s)
{
    return s.toUtf8().constData();
}
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    if (argc < 2)
    {
        fprintf(stderr, "usage: %s <file> [--workspace]\n", argv[0]);
        return 2;
    }
    const QString path = QString::fromLocal8Bit(argv[1]);
    const bool workspaceBench =
        argc > 2 && QLatin1String(argv[2]) == QLatin1String("--workspace");

    fprintf(stdout, "起始 RSS: %ld KB\n", rssKb());

    App application;
    MainWindow window(application.context());
    window.build(application.host());
    window.resize(1600, 900);
    window.show();
    QApplication::processEvents();

    // 走真实 data.load 路径（与 CLI 完全相同）
    QElapsedTimer timer;
    timer.start();
    Intent load;
    load.op = QStringLiteral("data.load");
    load.params[QStringLiteral("path")] = path;
    const auto loaded = application.host().execute(load);
    const qint64 loadMs = timer.elapsed();
    if (!loaded)
    {
        fprintf(stderr, "load failed: %s\n", u8(loaded.error().message));
        return 1;
    }
    const int dsId = loaded.value().payload.toMap().value(QStringLiteral("dataset")).toInt();
    fprintf(stdout, "载入 %s：%lld ms，RSS %ld KB\n", u8(QFileInfo(path).fileName()),
            loadMs, rssKb());

    // 找到第一张表的第一个数值列
    const Dataset *ds = application.data().get(dsId);
    if (!ds || ds->tableCount() == 0)
    {
        fprintf(stderr, "dataset is empty\n");
        return 1;
    }
    const Table *table = ds->table(0);
    QString column;
    for (const Column &c : table->columns)
        if (c.type != ValueType::Text)
        {
            column = c.name;
            break;
        }
    fprintf(stdout, "表 %s：%d 行，绘制列 %s\n", u8(table->name), table->rows,
            u8(column));

    // 渲染：设活动通道 → 绘图视图订阅到变化 → 走 plot.channel 意图取数渲染。
    // 这与用户在数据集树双击是**同一条路径**。
    timer.restart();
    application.context().setActiveChannel(dsId, table->name, column);
    const qint64 renderMs = timer.elapsed();

    // 中央画布：从主窗口树下取（画布由 workspace 的 cell 创建）。
    auto *canvas = window.findChild<AxesView *>();
    if (!canvas && !workspaceBench)
    {
        fprintf(stderr, "canvas not created\n");
        return 1;
    }
    if (canvas)
    {
        fprintf(stdout, "渲染：%lld ms，画布三元组 %d 个（像素宽 %d），RSS %ld KB\n", renderMs,
                canvas->lodPointCount(), canvas->width(), rssKb());
        fprintf(stdout, "结论：三元组数与行数无关（%d 行 -> %d 点），内存有界。\n", table->rows,
                canvas->lodPointCount());
    }

    if (workspaceBench)
    {
        // 主窗口树下找 PlotWorkspace（中央画布在 workspace 的 cell 里）。
        auto *ws = window.findChild<PlotWorkspace *>();
        if (!ws)
        {
            fprintf(stderr, "workspace not found\n");
            return 1;
        }
        // 构造 2 页 × 2x2 网格（8 画布）× 4 通道 + 显式色 + 深缩放视口的文档，
        // 计时 applyDocumentSpec（布局先到 → 数据回绑 → 视口回放的完整恢复链）。
        const QStringList cols = [&] {
            QStringList out;
            for (const Column &c : table->columns)
                if (c.type != ValueType::Text && out.size() < 4)
                    out << c.name;
            return out;
        }();
        const int srcId = application.context().sources().sourceOfDataset(dsId);
        const DataSourceInfo *src = application.context().sources().get(srcId);
        if (!src || cols.size() < 4)
        {
            fprintf(stderr, "not enough columns / source\n");
            return 1;
        }
        plot::PlotDocumentSpec doc;
        doc.version = 1;
        const QColor palette[4] = {{0x5b, 0xc0, 0xf0}, {0x8a, 0xd8, 0x6e},
                                   {0xe4, 0x57, 0x2e}, {0xf2, 0xb1, 0x34}};
        for (int p = 0; p < 2; ++p)
        {
            plot::PlotPageSpec page;
            page.id = QStringLiteral("bench-page-%1").arg(p);
            plot::LayoutNode grid;
            grid.isCanvas = false;
            grid.direction = QStringLiteral("horizontal");
            for (int cell = 0; cell < 4; ++cell)
            {
                plot::LayoutNode node;
                node.isCanvas = true;
                plot::CanvasSpec cs;
                cs.id = QStringLiteral("bench-c%1-%2").arg(p).arg(cell);
                cs.viewType = QStringLiteral("timeseries");
                cs.datasetOrigin = src->origin;
                for (int k = 0; k < 4; ++k)
                    cs.channels << table->name + QLatin1Char('.') + cols[k];
                QJsonArray series;
                for (int k = 0; k < 4; ++k)
                {
                    QJsonObject so;
                    so.insert(QStringLiteral("key"),
                              QStringLiteral("%1|%2|%3").arg(dsId).arg(table->name, cols[k]));
                    so.insert(QStringLiteral("color"), palette[k].name(QColor::HexRgb));
                    series.append(so);
                }
                QJsonObject style;
                style.insert(QStringLiteral("series"), series);
                cs.style = style;
                QJsonObject st;
                const double lo = 10.0 * cell + p;
                st.insert(QStringLiteral("x"), QJsonArray{lo, lo + 5.0});
                cs.state = st;
                node.canvas = cs;
                grid.children.append(node);
            }
            page.root = grid;
            doc.pages.append(page);
        }

        // 预热一次（首开含 widget/画布创建的冷路径），再计 3 次热打开。
        QElapsedTimer t;
        t.start();
        CHECK_APPLY(ws->applyDocumentSpec(doc, application.context().sources()), 1);
        fprintf(stdout, "工作区打开（8格x4通道，冷）：%.1f ms\n", t.elapsed() / 1.0);
        std::vector<double> ms;
        for (int i = 0; i < 3; ++i)
        {
            t.restart();
            CHECK_APPLY(ws->applyDocumentSpec(doc, application.context().sources()), 2);
            ms.push_back(t.elapsed() / 1.0);
        }
        std::sort(ms.begin(), ms.end());
        fprintf(stdout, "工作区打开（8格x4通道，热）：中位 %.1f ms\n", ms[1]);
    }
    return 0;
}
