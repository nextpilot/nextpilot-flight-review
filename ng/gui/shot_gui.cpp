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
#include "ProtocolTemplateStore.h"
#include "TransformEditorDialog.h"
#include "FilterEditorDialog.h"
#include "ImportDialog.h"
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

    // --import[=<path>]：早分支参数（截图通用导入对话框，#74/#75）。
    QString importArg;
    bool importArgSeen = false;
    for (int i = 2; i < argc; ++i)
    {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QStringLiteral("--import") || a.startsWith(QStringLiteral("--import=")))
        {
            importArg = a.startsWith(QStringLiteral("--import="))
                            ? a.mid(QStringLiteral("--import=").size()) : QString();
            importArgSeen = true;
        }
    }

    // --protocol <输出png> [模板路径] [样例帧hex] [页签序号]：截图协议编辑器（触发校验/试算）。
    // 给模板路径时按"打开现有协议"的真实路径载入（走 ProtocolTemplateStore，与 GUI 同一份）。
    if (out == QStringLiteral("--protocol") && argc >= 3)
    {
        const QString png = QString::fromLocal8Bit(argv[2]);
        const QString tplPath = argc >= 4 ? QString::fromLocal8Bit(argv[3]) : QString();
        const QString frameHex = argc >= 5 ? QString::fromLocal8Bit(argv[4]) : QString();
        const int tabIndex = argc >= 6 ? QString::fromLocal8Bit(argv[5]).toInt() : 0;
        const bool expectShot = argc >= 7 && QString::fromLocal8Bit(argv[6]) == QLatin1String("expect");
        const bool segShot = argc >= 7 && QString::fromLocal8Bit(argv[6]) == QLatin1String("seg");
        if (segShot)
        {
            qputenv("PLOTSCOPE_SHOT_SEG", (png + QStringLiteral("-seg.png")).toUtf8());
            if (argc >= 8)
                qputenv("PLOTSCOPE_SHOT_SEG_ROW", argv[7]);
        }
        nextpilot::plotscope::gui::ProtocolEditorDialog dlg;
        QByteArray tpl;
        QString terr;
        if (!tplPath.isEmpty() &&
            nextpilot::plotscope::host::ProtocolTemplateStore::read(tplPath, &tpl, &terr, false))
            dlg.setTemplateText(QString::fromUtf8(tpl), tplPath);

        // 样例帧框：不再靠"哪个框里有 {" 猜，改成找 placeholder/内容判定不可靠，
        // 直接用对话框的公开接口写样例帧。
        dlg.setSampleFrame(frameHex.isEmpty() ? QStringLiteral("AA 0C 00 01 00") : frameHex);
        // 触发"校验"与"试算单帧"按钮，让截图带上真实结果。
        for (auto *btn : dlg.findChildren<QPushButton *>())
        {
            if (btn->text() == QStringLiteral("校验") ||
                btn->text() == QStringLiteral("试算单帧"))
                btn->click();
        }
        dlg.selectTab(tabIndex);
        if (expectShot)
        {
            const int row = argc >= 8 ? QString::fromLocal8Bit(argv[7]).toInt() : 0;
            dlg.shotExpectLines(row, png + QStringLiteral("-expect.png"));
        }
        dlg.show();
        QCoreApplication::processEvents();
        QTimer::singleShot(expectShot ? 1600 : 200, [&] {
            const QPixmap shot = dlg.grab();
            if (shot.save(png))
                fprintf(stdout, "协议编辑器截图已保存: %s (%dx%d)\n", qPrintable(png),
                        shot.width(), shot.height());
            else
                fprintf(stderr, "协议编辑器截图保存失败: %s\n", qPrintable(png));
            if (!segShot)
                app.quit();
        });
        if (segShot)
        {
            dlg.openFirstSegmentDetail();
            return 0;
        }
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
    QString stateArg;     // --state=<table.column> 截状态视图色带（第二梯队 #203）
    QString offsetArg;    // --offset=<sourceId:seconds> 设源级时间偏移（#204）
    QString alignArg;     // --align=<start|center|end> 一键多源对齐（#204）
    QString legendArg;    // --legend=<collapse|zoom[±N]> 图例交互演示（#205）
    QString liveArg;      // --livesource=<名>[:paused] 注册演示 live 源（#193 暂停/继续按钮）
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
        if (a.startsWith(QStringLiteral("--state=")))
        {
            stateArg = a.mid(QStringLiteral("--state=").size());
            continue;
        }
        if (a.startsWith(QStringLiteral("--offset=")))
        {
            offsetArg = a.mid(QStringLiteral("--offset=").size());
            continue;
        }
        if (a.startsWith(QStringLiteral("--align=")))
        {
            alignArg = a.mid(QStringLiteral("--align=").size());
            continue;
        }
        if (a.startsWith(QStringLiteral("--legend=")))
        {
            legendArg = a.mid(QStringLiteral("--legend=").size());
            continue;
        }
        if (a.startsWith(QStringLiteral("--livesource=")))
        {
            liveArg = a.mid(QStringLiteral("--livesource=").size());
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

    // --import[=<path>]：截通用"打开数据"对话框（#74/#75 验收）。
    // 走真实对话框：schema 驱动渲染 + probeFile 探测 + 预览表，不硬编码任何格式。
    // 独立验收：不需要先加载数据文件。
    if (importArgSeen)
    {
        ImportDialog dlg(application.context().toolbox());
        if (!importArg.isEmpty())
            dlg.setPath(importArg); // 预填路径立即探测（与"打开方式"同路径）
        dlg.resize(760, 620);
        dlg.show();
        QCoreApplication::processEvents();
        const QPixmap shot = dlg.grab();
        if (shot.save(out))
            fprintf(stdout, "导入对话框截图已保存: %s (%dx%d) fields=%d format=%s previewRows=%d\n",
                    qPrintable(out), shot.width(), shot.height(),
                    dlg.fieldCount(), qPrintable(dlg.currentFormatId()), dlg.previewRowCount());
        else
            fprintf(stderr, "导入对话框截图保存失败: %s\n", qPrintable(out));
        return 0;
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

    // --livesource=<名>[:paused]：注册一个演示 live 源并挂上首数据集。
    // NG 当前没有真实 live 运行时，这里只登记运行时实例（SourceKind::Live），
    // 让数据源卡片呈现 live 形态：运行中显示暂停按钮 / 已暂停显示继续按钮（#193）。
    if (!liveArg.isEmpty())
    {
        DataSourceInfo liveInfo;
        const int colon = liveArg.indexOf(QLatin1Char(':'));
        liveInfo.name = colon > 0 ? liveArg.left(colon) : liveArg;
        const bool paused = liveArg.endsWith(QStringLiteral(":paused"));
        liveInfo.origin = paused ? QStringLiteral("udp://127.0.0.1:14550 (paused)")
                                 : QStringLiteral("udp://127.0.0.1:14550");
        liveInfo.kind = SourceKind::Live;
        liveInfo.state = paused ? SourceState::Paused : SourceState::Streaming;
        liveInfo.datasetIds = {firstDataset};
        const int liveId = application.context().sources().add(liveInfo);
        application.context().setActiveSourceId(liveId);
        fprintf(stdout, "演示 live 源已注册: id=%d name=%s state=%s\n", liveId,
                qPrintable(liveInfo.name), paused ? "Paused" : "Streaming");
    }

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

    // --state=<table.column>[,<table.column>…]：执行 data.state 意图（append 模式），
    // 把状态色带画到画布顶部后截图（第二梯队 #203：状态视图对齐 PJ4 状态转换视图）。
    if (!stateArg.isEmpty())
    {
        const QStringList stateChannels = stateArg.split(QLatin1Char(','));
        int drawnRows = -1;
        for (const QString &qualified : stateChannels)
        {
            const int dot = qualified.indexOf(QLatin1Char('.'));
            if (dot <= 0 || firstDataset < 0)
                continue;
            Intent stit;
            stit.op = QStringLiteral("data.state");
            stit.params[QStringLiteral("dataset")] = firstDataset;
            stit.params[QStringLiteral("table")] = qualified.left(dot);
            stit.params[QStringLiteral("column")] = qualified.mid(dot + 1);
            if (stateChannels.size() > 1)
                stit.params[QStringLiteral("append")] = true;
            const auto str = application.host().execute(stit);
            if (str)
            {
                drawnRows = str.value().payload.toMap().value(QStringLiteral("segments")).toInt();
                fprintf(stdout, "状态视图 %s: 成功，段数 %d\n",
                        qPrintable(qualified), drawnRows);
            }
            else
            {
                fprintf(stdout, "状态视图 %s 失败: %s；可用通道：\n",
                        qPrintable(qualified), qPrintable(str.error().message));
                if (const Dataset *sds = application.context().data().get(firstDataset))
                    for (int ti = 0; ti < sds->tableCount(); ++ti)
                    {
                        const Table *t = sds->table(ti);
                        if (!t)
                            continue;
                        for (const Column &c : t->columns)
                            fprintf(stdout, "  %s.%s (type=%d)\n",
                                    qPrintable(t->name), qPrintable(c.name), int(c.type));
                    }
            }
        }
    }

    // --offset=<sourceId:seconds>：源级显示时间偏移（第二梯队 #204 多源时间对齐）。
    // --align=<start|center|end>：一键把所有源的开始/中点/结束平移到同一基准。
    if (!offsetArg.isEmpty() || !alignArg.isEmpty())
    {
        auto runVa = [&application](const QString &op, const QVariantMap &params) {
            QVariant v;
            Result<QVariant> res(v);
            const bool hit = application.context().interactions().dispatchViewAction(op, params, res);
            if (!hit || !res.ok())
                fprintf(stdout, "%s: 失败 %s\n", qPrintable(op),
                        hit ? qPrintable(res.error().message) : "未注册");
            else
                fprintf(stdout, "%s: 成功 %s\n", qPrintable(op),
                        qPrintable(res.value().toString()));
        };
        if (!offsetArg.isEmpty())
        {
            // 形式一：<sourceId>:<sec>；形式二：last:<sec>（最后加载的源，便于双文件演示）。
            const int colon = offsetArg.indexOf(QLatin1Char(':'));
            if (colon > 0)
            {
                const QString idPart = offsetArg.left(colon);
                const double sec = offsetArg.mid(colon + 1).toDouble();
                int src = -1;
                if (idPart == QStringLiteral("last"))
                {
                    const QVector<int> srcIds = application.context().sources().ids();
                    if (!srcIds.isEmpty())
                        src = srcIds.last();
                }
                else
                    src = idPart.toInt();
                if (src >= 0)
                    runVa(QStringLiteral("plot.offset.set"),
                          {{QStringLiteral("sourceId"), src},
                           {QStringLiteral("offsetSec"), sec}});
            }
        }
        if (!alignArg.isEmpty())
            runVa(QStringLiteral("plot.align"),
                  {{QStringLiteral("mode"), alignArg}});
    }

    // --legend=<collapse|zoomN>：图例交互（第二梯队 #205）。
    // collapse = 折叠图例；zoomN = 字号缩放 N 步（正放大负缩小）。走 view action 同路径。
    if (!legendArg.isEmpty() && firstDataset >= 0)
    {
        QVariant lv;
        Result<QVariant> lres(lv);
        auto *wsx = qobject_cast<CanvasWorkspace *>(
            window.binder() ? window.binder()->centralContent() : nullptr);
        if (wsx && wsx->activeCanvas())
        {
            // 确保 activeCanvas 是活动 cell 的画布（view action 作用于它）。
            if (legendArg == QStringLiteral("collapse"))
                application.context().interactions().dispatchViewAction(
                    QStringLiteral("plot.legend.collapse"), {}, lres);
            else if (legendArg.startsWith(QStringLiteral("zoom")))
            {
                bool okSteps = false;
                const int steps = legendArg.mid(4).toInt(&okSteps);
                if (okSteps)
                    application.context().interactions().dispatchViewAction(
                        QStringLiteral("plot.legend.fontZoom"),
                        {{QStringLiteral("steps"), steps}}, lres);
            }
            fprintf(stdout, "图例交互 %s: %s\n", qPrintable(legendArg),
                    lres.ok() ? "成功" : qPrintable(lres.error().message));
        }
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
