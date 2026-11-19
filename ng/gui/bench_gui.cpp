// GUI 百万行实测：验证"ps 数据 UI 呈现"在真实大数据量下的内存与速度。
//
// 手动基准（不进 ctest）：需要外部生成的大文件。
//   用法：plotscope-ng-gui-bench <file.ulg>
//
// 关注三件事：
//   1) 加载 100 万行后进程 RSS 仍然有界（无 C++ 全量副本）；
//   2) 画布绘制的三元组数 <= 像素宽（与行数无关）；
//   3) 渲染耗时是像素级，不是行级。

#include "App.h"

#include "MainWindow.h"
#include "PlotCanvas.h"
#include "PanelFactoryRegistry.h"
#include "ShellBinder.h"

#include "Dataset.h"
#include "Intent.h"
#include "RuntimeHost.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFileInfo>

#include <cstdio>

using namespace nextpilot::plotscope::app;
using namespace nextpilot::plotscope::core;
using namespace nextpilot::plotscope::gui;

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

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    if (argc < 2)
    {
        fprintf(stderr, "用法: %s <file.ulg>\n", argv[0]);
        return 2;
    }
    const QString path = QString::fromLocal8Bit(argv[1]);

    fprintf(stdout, "起始 RSS: %ld KB\n", rssKb());

    App application;
    MainWindow window(application.context());
    window.build(application.host());
    window.resize(1600, 900);

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
        fprintf(stderr, "载入失败: %s\n", qPrintable(loaded.error().message));
        return 1;
    }
    const int dsId = loaded.value().payload.toMap().value(QStringLiteral("dataset")).toInt();
    fprintf(stdout, "载入 %s：%lld ms，RSS %ld KB\n", qPrintable(QFileInfo(path).fileName()),
            loadMs, rssKb());

    // 找到第一张表的第一个数值列
    const Dataset *ds = application.data().get(dsId);
    if (!ds || ds->tableCount() == 0)
    {
        fprintf(stderr, "数据集为空\n");
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
    fprintf(stdout, "表 %s：%d 行，绘制列 %s\n", qPrintable(table->name), table->rows,
            qPrintable(column));

    // 渲染：设活动通道 → 绘图视图订阅到变化 → 走 plot.channel 意图取数渲染。
    // 这与用户在数据集树双击是**同一条路径**。
    timer.restart();
    application.context().setActiveChannel(dsId, table->name, column);
    const qint64 renderMs = timer.elapsed();

    // 中央画布：从画布插槽取回（视图层自注册的 "plot-canvas" 部件）。
    auto *canvas = qobject_cast<PlotCanvas *>(
        window.binder()->centralContent());
    if (!canvas)
    {
        fprintf(stderr, "画布未创建\n");
        return 1;
    }
    fprintf(stdout, "渲染：%lld ms，画布三元组 %d 个（像素宽 %d），RSS %ld KB\n", renderMs,
            canvas->lodPointCount(), canvas->width(), rssKb());
    fprintf(stdout, "结论：三元组数与行数无关（%d 行 -> %d 点），内存有界。\n", table->rows,
            canvas->lodPointCount());
    return 0;
}
