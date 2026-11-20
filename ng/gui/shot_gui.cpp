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
#include "RuntimeHost.h"
#include "PlotCanvas.h"
#include "CanvasWorkspace.h"
#include "ProtocolEditorDialog.h"
#include "TransformEditorDialog.h"
#include "FilterEditorDialog.h"
#include "Transform.h"
#include "ShellBinder.h"
#include "SARibbonCategory.h"
#include "SARibbonBar.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QTimer>

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

    // --protocol <输出png>：截图协议编辑器（填好样例帧并触发校验/试算）。
    if (out == QStringLiteral("--protocol") && argc >= 3)
    {
        const QString png = QString::fromLocal8Bit(argv[2]);
        nextpilot::plotscope::gui::ProtocolEditorDialog dlg;
        // 对话框里有两个 QPlainTextEdit（JSON 模板框 + hex 样例框）。
        // 不依赖构造顺序：含 "{" 的是 JSON 框（保持 blankTemplate 不动），
        // 另一个是 hex 样例框，写入样例帧。
        for (auto *edit : dlg.findChildren<QPlainTextEdit *>())
        {
            if (!edit->toPlainText().trimmed().startsWith(QLatin1Char('{')))
                edit->setPlainText(QStringLiteral("AA 0C 00 01 00"));
        }
        // 触发"校验"与"试算单帧"按钮，让截图带上真实结果。
        for (auto *btn : dlg.findChildren<QPushButton *>())
        {
            if (btn->text() == QStringLiteral("校验") ||
                btn->text() == QStringLiteral("试算单帧"))
                btn->click();
        }
        dlg.show();
        QCoreApplication::processEvents();
        QTimer::singleShot(200, [&] {
            const QPixmap shot = dlg.grab();
            if (shot.save(png))
                fprintf(stdout, "协议编辑器截图已保存: %s (%dx%d)\n", qPrintable(png),
                        shot.width(), shot.height());
            else
                fprintf(stderr, "协议编辑器截图保存失败: %s\n", qPrintable(png));
            app.quit();
        });
        return app.exec();
    }
    // 末尾若有两个纯数字，视为宽高；若出现 "dark"/"light" 关键字，视为初始主题。
    // --type=<kind>：加载后触发图库里的图型 action（走真实 dispatcher 链路）。
    QStringList files;
    QString themeArg;
    QString plotTypeArg;
    QString tabArg;
    QString transformArg; // --transform=<cap> 截变换编辑器（cap: derived/axis-y/checksum）
    QString cursorArg;    // --cursor=<t> 触发游标读数（老代码交互欠账验收用）
    QString scriptArg;    // --script=<lua源码> 用脚本 app 全局操作应用（脚本对齐验收用）
    QString filterArg;    // --filter=<id> 截过滤器编辑器（脚本对齐 C：FilterOps+表单）
    QString markerArg;    // --marker=<lua规则> 截 Marker 规则叠加（脚本对齐 D）
    QStringList actionArgs;
    int w = 1440;
    int h = 900;
    for (int i = 2; i < argc; ++i)
    {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QStringLiteral("dark") || a == QStringLiteral("light"))
        {
            themeArg = a;
            continue;
        }
        if (a.startsWith(QStringLiteral("--type=")))
        {
            plotTypeArg = a.mid(QStringLiteral("--type=").size());
            continue;
        }
        if (a.startsWith(QStringLiteral("--tab=")))
        {
            tabArg = a.mid(QStringLiteral("--tab=").size());
            continue;
        }
        if (a.startsWith(QStringLiteral("--transform=")))
        {
            transformArg = a.mid(QStringLiteral("--transform=").size());
            continue;
        }
        if (a.startsWith(QStringLiteral("--action=")))
        {
            actionArgs << a.mid(QStringLiteral("--action=").size());
            continue;
        }
        if (a.startsWith(QStringLiteral("--cursor=")))
        {
            cursorArg = a.mid(QStringLiteral("--cursor=").size());
            continue;
        }
        if (a.startsWith(QStringLiteral("--script=")))
        {
            scriptArg = a.mid(QStringLiteral("--script=").size());
            continue;
        }
        if (a.startsWith(QStringLiteral("--filter")))
        {
            filterArg = a.startsWith(QStringLiteral("--filter="))
                ? a.mid(QStringLiteral("--filter=").size()) : QStringLiteral("scale");
            continue;
        }
        if (a.startsWith(QStringLiteral("--marker=")))
        {
            markerArg = a.mid(QStringLiteral("--marker=").size());
            continue;
        }
        files << a;
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
    window.build(application.host());
    window.resize(w, h);

    // 若指定主题，走与按钮/脚本相同的 theme.set 意图路径切换。
    if (!themeArg.isEmpty())
    {
        Intent theme;
        theme.op = QStringLiteral("theme.set");
        theme.params[QStringLiteral("theme")] = themeArg;
        application.host().execute(theme);
    }

    // 先 show()，让布局（停靠区 + 画布尺寸）确定下来。
    //
    // 顺序很关键：LOD 的桶数必须等于画布实际像素宽，桶数不匹配就会出现
    // "梳齿"假象（每桶跨多个采样周期，min/max 被撑到值域两端）。
    // 画布尺寸只有在窗口显示并完成布局后才是最终值——
    // 所以取数与选通道都放在 show() 之后。
    window.show();
    QCoreApplication::processEvents();

    // --tab=<页名>：切到指定 ribbon 页（按 categoryTitle 匹配），用于逐页截图核对 8 页布局。
    if (!tabArg.isEmpty())
    {
        if (SARibbonBar *ribbon = window.ribbonBar())
        {
            bool switched = false;
            const auto cats = ribbon->categoryPages();
            for (int i = 0; i < cats.size(); ++i)
            {
                if (cats.at(i)->categoryName() == tabArg)
                {
                    ribbon->setCurrentIndex(i);
                    switched = true;
                    break;
                }
            }
            fprintf(stdout, "ribbon 页 %s: %s\n", qPrintable(tabArg),
                    switched ? "已切换" : "未找到");
        }
        QCoreApplication::processEvents();
    }

    // 载入数据（与 CLI 完全相同的数据.load 意图路径）。
    // 多个文件 = 多个数据源同时打开。
    int firstDataset = -1;
    for (const QString &path : files)
    {
        Intent load;
        load.op = QStringLiteral("data.load");
        load.params[QStringLiteral("path")] = path;
        const auto loaded = application.host().execute(load);
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

    // 依次叠加全部数值通道：每次 setActiveChannel 等价于在数据集树双击一列，
    // ChannelView 订阅后走 addChannel 追加一条曲线。这样截图呈现的是本轮的
    // 核心能力——多曲线叠加，而不是只画一条。
    const Dataset *ds = application.data().get(firstDataset);
    if (ds && ds->tableCount() > 0)
    {
        const Table *table = ds->table(0);
        for (const Column &c : table->columns)
        {
            if (c.type != ValueType::Text)
                application.context().setActiveChannel(firstDataset, table->name, c.name);
        }
    }

    // 若指定 --type=<kind>：触发对应图库 action，走真实 dispatcher
    // （图型 → plot.setType view action → 曲线本画布切 / 仪表切 InstrumentView）。
    if (!plotTypeArg.isEmpty())
    {
        const QString targetId = QStringLiteral("plot.type.") + plotTypeArg;
        bool triggered = false;
        for (QAction *a : window.findChildren<QAction *>())
        {
            if (a->data().toString() == targetId)
            {
                a->trigger();
                triggered = true;
                break;
            }
        }
        fprintf(stdout, "图型 action %s: %s\n", qPrintable(targetId),
                triggered ? "已触发" : "未找到");
    }

    // 通用 view action 触发（如 page.splitHorizontal / page.grid）：
    // 走与 ribbon 点击相同的 AppContext 派发路径。
    for (const QString &actionId : actionArgs)
    {
        QVariantMap params;
        if (actionId == QStringLiteral("page.grid"))
        {
            params.insert(QStringLiteral("rows"), 2);
            params.insert(QStringLiteral("cols"), 1);
        }
        QVariant vr;
        Result<QVariant> res(vr);
        const bool hit = application.context().interactions().dispatchViewAction(actionId, params, res);
        fprintf(stdout, "view action %s: %s\n", qPrintable(actionId),
                hit ? (res ? "成功" : "失败") : "未注册");
    }

    // --script=<lua>：通过 script.run 意图执行脚本，脚本内 app.call 可操作
    // 意图通道 + view action 通道，验证"脚本直接引用 app 操作整个应用"。
    if (!scriptArg.isEmpty())
    {
        Intent sc;
        sc.op = QStringLiteral("script.run");
        sc.params[QStringLiteral("source")] = scriptArg.toUtf8();
        const auto sr = application.host().execute(sc);
        fprintf(stdout, "app 脚本: %s\n", sr ? "执行成功" : qPrintable(sr.error().message));
    }

    // --marker=<lua规则>：执行 data.marker 意图，把 PlotMarker 画到画布后截图。
    if (!markerArg.isEmpty())
    {
        const Dataset *mds = application.context().data().get(firstDataset);
        if (mds && mds->tableCount() > 0 && mds->table(0) && !mds->table(0)->columns.isEmpty())
        {
            Intent mit;
            mit.op = QStringLiteral("data.marker");
            mit.params[QStringLiteral("dataset")] = firstDataset;
            mit.params[QStringLiteral("table")] = mds->table(0)->name;
            mit.params[QStringLiteral("column")] = mds->table(0)->columns.first().name;
            mit.params[QStringLiteral("code")] = markerArg.toUtf8();
            const auto mr = application.host().execute(mit);
            fprintf(stdout, "Marker 规则: %s\n", mr ? "执行成功" : qPrintable(mr.error().message));
            if (mr)
                fprintf(stdout, "产出 marker: %d\n",
                        mr.value().payload.toMap().value(QStringLiteral("count")).toInt());
        }
        else
            fprintf(stderr, "Marker 规则: 无可用数据集/通道\n");
    }

    // --cursor=<t>：走真实画布 API 触发游标读数（老代码交互欠账验收）。
    // 同时演示 A/B 与书签，让截图能一眼看到读数框、区间带、书签 chip。
    if (!cursorArg.isEmpty())
    {
        auto *ws = qobject_cast<CanvasWorkspace *>(window.binder() ? window.binder()->centralContent() : nullptr);
        PlotCanvas *pc = ws ? ws->activeCanvas() : nullptr;
        bool ok = false;
        const double t = cursorArg.toDouble(&ok);
        if (pc && ok)
        {
            pc->setCursorTime(t, true);          // 固定读数
            pc->toggleAB();                      // 生成 A/B（以游标为中心）
            pc->addBookmark(t, QStringLiteral("游标书签"));
            fprintf(stdout, "交互注入: cursor t=%f, ab=%d, bookmarks=%d\n",
                    t, int(pc->abActive()), int(pc->bookmarks().size()));
        }
        else
        {
            fprintf(stderr, "交互注入失败: canvas=%p, cursor=%s\n", (void *)pc, qPrintable(cursorArg));
        }
        QCoreApplication::processEvents();
    }

    // --filter[=<id>]：截过滤器编辑器（脚本对齐 C：FilterOps 列表 + 参数表单）。
    if (!filterArg.isEmpty())
    {
        FilterEditorDialog dlg(application.context(),
                               filterArg.isEmpty() ? QString() : filterArg, &window);
        if (!filterArg.isEmpty())
            dlg.setCurrentFilter(filterArg);
        dlg.show();
        QCoreApplication::processEvents();
        QTimer::singleShot(200, [&] {
            const QPixmap shot = dlg.grab();
            if (shot.save(out))
                fprintf(stdout, "过滤器编辑器截图已保存: %s\n", qPrintable(out));
            else
                fprintf(stderr, "过滤器编辑器截图保存失败\n");
            app.quit();
        });
        return app.exec();
    }

    // --transform=<cap>：截变换编辑器（展示按能力过滤 + 自说明 + 在线调试）。
    if (!transformArg.isEmpty())
    {
        uint32_t cap = CapAxisY;
        if (transformArg == QStringLiteral("derived")) cap = CapDerived;
        else if (transformArg == QStringLiteral("axis-x")) cap = CapAxisX;
        else if (transformArg == QStringLiteral("checksum")) cap = CapChecksum;
        TransformEditorDialog dlg(application.context(), cap,
                                  capabilityNames(cap), &window);
        dlg.show();
        QCoreApplication::processEvents();
        QTimer::singleShot(200, [&] {
            const QPixmap shot = dlg.grab();
            if (shot.save(out))
                fprintf(stdout, "变换编辑器截图已保存: %s\n", qPrintable(out));
            else
                fprintf(stderr, "变换编辑器截图保存失败\n");
            app.quit();
        });
        return app.exec();
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
