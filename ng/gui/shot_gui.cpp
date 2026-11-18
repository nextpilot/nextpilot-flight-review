// GUI 截图工具：加载数据 → 渲染真实界面 → 存 PNG。
//
// 用途：让人直接看到当前界面长什么样（而不是靠描述猜）。
// 手动运行，不进 ctest：
//   QT_QPA_PLATFORM=offscreen plotscope-ng-gui-shot <输出png> <数据文件...> [宽 高]
//
// 关键点：走**真实交互路径**——设置活动通道 → 绘图视图订阅 → 走意图层取数 → 画布渲染，
// 所以截图里的曲线是真实数据，不是画出来的示意图。
//
// 支持**多个数据文件**：对应需求「app 支持同时打开多个数据源」——
// 数据集树里会同时列出多个来源，界面呈现的是真实的多源状态。

#include "App.h"

#include "AppContext.h"
#include "Dataset.h"
#include "Intent.h"
#include "MainWindow.h"
#include "ModuleHost.h"
#include "PlotCanvas.h"
#include "ShellBinder.h"

#include <QApplication>
#include <QCoreApplication>
#include <QPixmap>
#include <QTimer>

#include <cstdio>

using namespace ng::app;
using namespace ng::core;
using namespace ng::gui;

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    if (argc < 3)
    {
        fprintf(stderr, "用法: %s <输出png> <数据文件...> [宽 高]\n", argv[0]);
        return 2;
    }
    const QString out = QString::fromLocal8Bit(argv[1]);
    // 末尾若有两个纯数字，视为宽高。
    QStringList files;
    int w = 1440;
    int h = 900;
    for (int i = 2; i < argc; ++i)
    {
        files << QString::fromLocal8Bit(argv[i]);
    }
    if (files.size() >= 3)
    {
        bool okW = false, okH = false;
        const int aw = files.at(files.size() - 2).toInt(&okW);
        const int ah = files.at(files.size() - 1).toInt(&okH);
        if (okW && okH && aw > 200 && ah > 200)
        {
            w = aw;
            h = ah;
            files.removeLast();
            files.removeLast();
        }
    }

    App application;
    MainWindow window(application.context());
    window.build(application.modules());
    window.resize(w, h);

    // 先 show()，让布局（停靠区 + 画布尺寸）确定下来。
    //
    // 顺序很关键：LOD 的桶数必须等于画布实际像素宽，桶数不匹配就会出现
    // "梳齿"假象（每桶跨多个采样周期，min/max 被撑到值域两端）。
    // 画布尺寸只有在窗口显示并完成布局后才是最终值——
    // 所以取数与选通道都放在 show() 之后。
    window.show();
    QCoreApplication::processEvents();

    // 载入数据（与 CLI 完全相同的数据.load 意图路径）。
    // 多个文件 = 多个数据源同时打开。
    int firstDataset = -1;
    for (const QString &path : files)
    {
        Intent load;
        load.op = QStringLiteral("data.load");
        load.params[QStringLiteral("path")] = path;
        const auto loaded = application.modules().execute(load);
        if (!loaded)
        {
            fprintf(stderr, "载入失败(%s): %s\n", qPrintable(path),
                    qPrintable(loaded.error().message));
            continue;
        }
        const int dsId = loaded.value().payload.toMap().value(QStringLiteral("dataset")).toInt();
        if (firstDataset < 0)
            firstDataset = dsId;
    }
    if (firstDataset < 0)
        return 1;

    // 选中第一条数值通道（与用户在数据集树双击同一条路径）
    const Dataset *ds = application.data().get(firstDataset);
    if (ds && ds->tableCount() > 0)
    {
        const Table *table = ds->table(0);
        for (const Column &c : table->columns)
        {
            if (c.type != ValueType::Text)
            {
                application.context().setActiveChannel(firstDataset, table->name, c.name);
                break;
            }
        }
    }

    // 让事件循环转一圈，ribbon/dock/画布完成布局与绘制
    QTimer::singleShot(600, [&] {
        const QPixmap shot = window.grab();
        if (shot.save(out))
            fprintf(stdout, "截图已保存: %s (%dx%d)\n", qPrintable(out), shot.width(),
                    shot.height());
        else
            fprintf(stderr, "截图保存失败: %s\n", qPrintable(out));
        app.quit();
    });
    return app.exec();
}
