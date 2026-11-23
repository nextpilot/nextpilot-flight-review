// ps_ui_driver —— offscreen UI 自动化驱动（截图 + Qt 事件注入），唯一的 UI 工具。
//
// 职责（吸收原 plotscope-gui-shot，见 issue IKX638）：
//   1) 交互驱动：JSON 步骤脚本，事件注入模拟用户操作；
//   2) 文档产图：场景即 JSON 脚本（docs/screenshots/scripts/），改场景不改代码。
//
// 用法：
//   ps_ui_driver --script steps.json [--out-dir DIR] [--width W] [--height H]
//
// 脚本为 JSON，steps 顺序执行，任一步失败计入失败数，结束按失败数退出：
//   { "steps": [
//     { "op": "list-pages" },
//     { "op": "load", "path": "tests/fixture/demo_flight.csv", "format": "csv" },
//     { "op": "plot-all-channels" },                       // 数据集全部数值通道上图
//     { "op": "page", "name": "数据" },                    // 按 title 切 ribbon 页（或 "index":N）
//     { "op": "settle", "ms": 150 },
//     { "op": "shot", "out": "data.png" },
//     { "op": "intent", "intent": "theme.set", "params": { "theme": "dark" } },
//     { "op": "view-action", "action": "figure.grid", "params": { "rows": 2, "cols": 1 } },
//     { "op": "panel", "slot": "dock.diagnostics", "panel": "diagnostics" },
//     { "op": "raise-panel", "area": "dock.left", "class": "DataManagerPanel" },
//     { "op": "cursor", "t": 12.5 },                       // 游标读数 + A/B + 书签
//     { "op": "hover-toolbar" },                           // 浮出活动格工具栏
//     { "op": "demo-live", "name": "live-demo", "paused": false }, // 注册演示 live 源
//     { "op": "click", "widget": "objectName" },
//     { "op": "activity-badge", "id": "diagnostics", "count": 3 }, // Activity 徽标
//     { "op": "key", "widget": "objectName", "keys": "Ctrl+S" },
//     { "op": "type", "widget": "objectName", "text": "hello" },
//     { "op": "action", "action": "plot.type.bar" },       // QAction（objectName/text/data 三匹配）
//     { "op": "dialog", "name": "protocol", "shot": "protocol.png",
//       "template": "x.psproto", "sample": "AA 0C 00 01 00", "tab": 1 },
//     { "op": "dump", "out": "tree.txt" }
//   ] }
//
// dialog 处理器（业务胶水，自 shot_gui 移植）：import / protocol / transform / filter / settings。
// widget 用 objectName 匹配（findChild 递归查找）；dump 输出控件树辅助定位。
// 拖拽（QDrag 程序化投递）暂未纳入，见 ps_test_gui 的 dropChannel 覆盖。

#include "App.h"
#include "AppContext.h"
#include "MainWindow.h"
#include "ShellBinder.h"
#include "GuiContext.h"
#include "DataManagerPanel.h"
#include "ImportDialog.h"
#include "ProtocolEditorDialog.h"
#include "ProtocolTemplateStore.h"
#include "TransformEditorDialog.h"
#include "Transform.h"
#include "FilterEditorDialog.h"
#include "PreferencesDialog.h"
#include "PlotWorkspace.h"
#include "AxesView.h"
#include "RuntimeHost.h"
#include "DataSource.h"
#include "Source.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QTextStream>
#include <SARibbonBar.h>
#include <SARibbonCategory.h>
#include <QTest>
#include <cstdio>

using namespace nextpilot::plotscope::app;
using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::context;
using namespace nextpilot::plotscope::gui;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::host;

namespace
{

struct Options
{
    QString scriptPath;
    QString outDir = QStringLiteral(".");
    int width = 1440;
    int height = 900;
};

Options parseArgs(int argc, char *argv[])
{
    Options opt;
    for (int i = 1; i < argc; ++i)
    {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QStringLiteral("--script") && i + 1 < argc)
            opt.scriptPath = QString::fromLocal8Bit(argv[++i]);
        else if (a == QStringLiteral("--out-dir") && i + 1 < argc)
            opt.outDir = QString::fromLocal8Bit(argv[++i]);
        else if (a == QStringLiteral("--width") && i + 1 < argc)
            opt.width = QString::fromLocal8Bit(argv[++i]).toInt();
        else if (a == QStringLiteral("--height") && i + 1 < argc)
            opt.height = QString::fromLocal8Bit(argv[++i]).toInt();
    }
    return opt;
}

QString resolvePath(const QString &outDir, const QString &name)
{
    return QDir(outDir).absoluteFilePath(name);
}

// 组合键 "Ctrl+S" / "Shift+Tab" / "Return" → QTest::keyClick
void pressKeyCombo(QWidget *target, const QString &combo)
{
    const QStringList tokens = combo.split(u'+', Qt::SkipEmptyParts);
    Qt::KeyboardModifiers mods;
    const QString keyName = tokens.last();
    for (int i = 0; i + 1 < tokens.size(); ++i)
    {
        const QString t = tokens[i].toLower();
        if (t == QStringLiteral("ctrl")) mods |= Qt::ControlModifier;
        else if (t == QStringLiteral("shift")) mods |= Qt::ShiftModifier;
        else if (t == QStringLiteral("alt")) mods |= Qt::AltModifier;
        else if (t == QStringLiteral("meta")) mods |= Qt::MetaModifier;
    }
    const Qt::Key key = QKeySequence(keyName)[0].key();
    QTest::keyClick(target, key, mods);
}

void dumpWidgetTree(QWidget *root, QTextStream &ts, int depth)
{
    for (QObject *child : root->children())
    {
        auto *w = qobject_cast<QWidget *>(child);
        if (!w)
            continue;
        ts << QString().repeated(depth * 2) << w->metaObject()->className()
           << "  name=" << w->objectName()
           << "  geo=" << w->geometry().width() << "x" << w->geometry().height()
           << "\n";
        dumpWidgetTree(w, ts, depth + 1);
    }
}

QVariantMap jsonToVariantMap(const QJsonObject &obj)
{
    return obj.toVariantMap();
}

// ---- dialog 处理器：业务胶水，自 shot_gui 原样移植。配置 + 显示 + 截图 + 关闭。----

bool shotDialog(QWidget &dlg, const QString &path, int waitMs)
{
    dlg.show();
    QTest::qWait(waitMs);
    const QPixmap shot = dlg.grab();
    if (!shot.save(path))
    {
        fprintf(stderr, "对话框截图保存失败: %s\n", qPrintable(path));
        return false;
    }
    fprintf(stdout, "对话框截图: %s (%dx%d)\n", qPrintable(path), shot.width(), shot.height());
    dlg.close();
    return true;
}

bool dialogImport(App &application, const QJsonObject &params, const QString &path)
{
    ImportDialog dlg(application.context().toolbox());
    const QString p = params.value(QStringLiteral("path")).toString();
    if (!p.isEmpty())
        dlg.setPath(p); // 预填路径立即探测（与"打开方式"同路径）
    dlg.resize(760, 620);
    return shotDialog(dlg, path, 100);
}

bool dialogProtocol(const QJsonObject &params, const QString &path)
{
    ProtocolEditorDialog dlg;
    const QString tplPath = params.value(QStringLiteral("template")).toString();
    if (!tplPath.isEmpty())
    {
        QByteArray tpl;
        QString terr;
        if (ProtocolTemplateStore::read(tplPath, &tpl, &terr, false))
            dlg.setTemplateText(QString::fromUtf8(tpl), tplPath);
        else
            fprintf(stderr, "模板读取失败: %s\n", qPrintable(terr));
    }
    const QString sample = params.value(QStringLiteral("sample")).toString();
    dlg.setSampleFrame(sample.isEmpty() ? QStringLiteral("AA 0C 00 01 00") : sample);
    // 点"校验"与"试算单帧"，让截图带上真实结果。
    for (auto *btn : dlg.findChildren<QPushButton *>())
    {
        if (btn->text() == QStringLiteral("校验") || btn->text() == QStringLiteral("试算单帧"))
            btn->click();
    }
    dlg.selectTab(params.value(QStringLiteral("tab")).toInt(0));

    const QString mode = params.value(QStringLiteral("mode")).toString();
    if (mode == QStringLiteral("seg"))
    {
        qputenv("PLOTSCOPE_SHOT_SEG", (path + QStringLiteral("-seg.png")).toUtf8());
        if (params.contains(QStringLiteral("row")))
            qputenv("PLOTSCOPE_SHOT_SEG_ROW",
                    QByteArray::number(params.value(QStringLiteral("row")).toInt()));
    }
    const bool ok = shotDialog(dlg, path, mode.isEmpty() ? 200 : 1600);
    if (mode == QStringLiteral("expect"))
    {
        const int row = params.value(QStringLiteral("row")).toInt(0);
        dlg.shotExpectLines(row, path + QStringLiteral("-expect.png"));
    }
    return ok;
}

bool dialogTransform(App &application, const QJsonObject &params, const QString &path)
{
    const QString capArg = params.value(QStringLiteral("cap")).toString();
    uint32_t cap = CapAxisY;
    if (capArg == QStringLiteral("derived")) cap = CapDerived;
    else if (capArg == QStringLiteral("axis-x")) cap = CapAxisX;
    else if (capArg == QStringLiteral("checksum")) cap = CapChecksum;
    TransformEditorDialog dlg(application.context(), cap, capabilityNames(cap));
    return shotDialog(dlg, path, 200);
}

bool dialogFilter(App &application, MainWindow &window, const QJsonObject &params, const QString &path)
{
    const QString id = params.value(QStringLiteral("id")).toString();
    FilterEditorDialog dlg(application.context(), id, &window);
    if (!id.isEmpty())
        dlg.setCurrentFilter(id);
    return shotDialog(dlg, path, 200);
}

bool dialogSettings(App &application, MainWindow &window, const QJsonObject &params, const QString &path)
{
    // 偏好设置（全局，App 作用域）：旧「数据设置」对话框已并入这里，
    // 场景名沿用 "settings"，可用 section 指定首屏分组（appearance/timeline/data）。
    const QString section = params.value(QStringLiteral("section")).toString();
    PreferencesDialog dlg(application.context(), application.host(), section, &window);
    dlg.show();
    dlg.adjustSize();
    return shotDialog(dlg, path, 250);
}

} // namespace

int main(int argc, char *argv[])
{
    const Options opt = parseArgs(argc, argv);
    if (opt.scriptPath.isEmpty())
    {
        fprintf(stderr, "用法: ps_ui_driver --script steps.json [--out-dir DIR] [--width W] [--height H]\n");
        return 2;
    }

    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication qapp(argc, argv);

    App application;
    MainWindow window(application.context());
    window.build(application.host());
    window.resize(opt.width, opt.height);
    window.show();
    QTest::qWait(200);

    QFile scriptFile(opt.scriptPath);
    if (!scriptFile.open(QIODevice::ReadOnly))
    {
        fprintf(stderr, "无法打开脚本: %s\n", qPrintable(opt.scriptPath));
        return 2;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(scriptFile.readAll());
    if (!doc.isObject())
    {
        fprintf(stderr, "脚本不是合法 JSON 对象: %s\n", qPrintable(opt.scriptPath));
        return 2;
    }
    const QJsonArray steps = doc.object().value(QStringLiteral("steps")).toArray();
    if (steps.isEmpty())
    {
        fprintf(stderr, "脚本缺少 steps 数组\n");
        return 2;
    }

    QDir().mkpath(opt.outDir);
    int failures = 0;
    int lastDataset = -1; // 最近一次 load/intent 产出的数据集 id
    int lastSource = -1;  // 最近一次 intent 产出的源 id（如 data.connect）

    auto fail = [&failures](const QString &op, const QString &msg) {
        ++failures;
        fprintf(stderr, "[失败] %s: %s\n", qPrintable(op), qPrintable(msg));
    };
    auto ok = [](const QString &msg) {
        fprintf(stdout, "[ok] %s\n", qPrintable(msg));
    };

    for (int i = 0; i < steps.size(); ++i)
    {
        const QJsonObject step = steps[i].toObject();
        const QString op = step.value(QStringLiteral("op")).toString();
        QApplication::processEvents();

        if (op == QStringLiteral("list-pages"))
        {
            SARibbonBar *ribbon = qobject_cast<SARibbonBar *>(window.menuWidget());
            if (!ribbon)
            {
                fail(op, QStringLiteral("取不到 SARibbonBar"));
                continue;
            }
            const QList<SARibbonCategory *> pages = ribbon->categoryPages(true);
            for (int p = 0; p < pages.size(); ++p)
                fprintf(stdout, "  页[%d] %s\n", p, qPrintable(pages[p]->categoryName()));
            ok(QStringLiteral("list-pages 共 %1 页").arg(pages.size()));
        }
        else if (op == QStringLiteral("page"))
        {
            SARibbonBar *ribbon = qobject_cast<SARibbonBar *>(window.menuWidget());
            if (!ribbon)
            {
                fail(op, QStringLiteral("取不到 SARibbonBar"));
                continue;
            }
            const QList<SARibbonCategory *> pages = ribbon->categoryPages(true);
            int index = -1;
            if (step.contains(QStringLiteral("name")))
            {
                const QString name = step.value(QStringLiteral("name")).toString();
                for (int p = 0; p < pages.size(); ++p)
                    if (pages[p]->categoryName() == name)
                    {
                        index = p;
                        break;
                    }
                if (index < 0)
                {
                    fail(op, QStringLiteral("未找到页: %1").arg(name));
                    continue;
                }
            }
            else
            {
                index = step.value(QStringLiteral("index")).toInt(-1);
                if (index < 0 || index >= pages.size())
                {
                    fail(op, QStringLiteral("页索引越界: %1").arg(index));
                    continue;
                }
            }
            ribbon->setCurrentIndex(index);
            QApplication::processEvents();
            ok(QStringLiteral("切到 ribbon 页 %1").arg(index));
        }
        else if (op == QStringLiteral("settle"))
        {
            const int ms = step.value(QStringLiteral("ms")).toInt(100);
            QTest::qWait(ms);
            QApplication::processEvents();
            ok(QStringLiteral("等待 %1ms").arg(ms));
        }
        else if (op == QStringLiteral("shot"))
        {
            const QString path = resolvePath(opt.outDir, step.value(QStringLiteral("out")).toString());
            if (!window.grab().save(path))
            {
                fail(op, QStringLiteral("保存失败: %1").arg(path));
                continue;
            }
            ok(QStringLiteral("截图 %1").arg(path));
        }
        else if (op == QStringLiteral("load"))
        {
            Intent intent;
            intent.op = QStringLiteral("data.load");
            intent.params[QStringLiteral("path")] = step.value(QStringLiteral("path")).toString();
            if (step.contains(QStringLiteral("format")))
                intent.params[QStringLiteral("format")] = step.value(QStringLiteral("format")).toString();
            const Result<IntentResult> r = application.host().execute(intent);
            if (!r)
            {
                fail(op, QStringLiteral("data.load 失败: %1").arg(r.error().message));
                continue;
            }
            lastDataset = r.value().payload.toMap().value(QStringLiteral("dataset")).toInt();
            ok(QStringLiteral("data.load %1 -> 数据集 %2").arg(step.value(QStringLiteral("path")).toString()).arg(lastDataset));
        }
        else if (op == QStringLiteral("plot-all-channels"))
        {
            // 数据集全部数值通道上图：每次 setActiveChannel 等价于在数据集树双击一列。
            const int dsId = step.value(QStringLiteral("dataset")).toInt(lastDataset);
            const Dataset *ds = application.context().data().get(dsId);
            if (!ds || ds->tableCount() == 0)
            {
                fail(op, QStringLiteral("无可用数据集/表 (dataset=%1)").arg(dsId));
                continue;
            }
            const Table *table = ds->table(0);
            int added = 0;
            for (const Column &c : table->columns)
            {
                if (c.type != ValueType::Text)
                {
                    application.context().setActiveChannel(dsId, table->name, c.name);
                    ++added;
                }
            }
            QApplication::processEvents();
            ok(QStringLiteral("通道上图 %1 条（数据集 %2）").arg(added).arg(dsId));
        }
        else if (op == QStringLiteral("intent"))
        {
            Intent intent;
            intent.op = step.value(QStringLiteral("intent")).toString();
            intent.params = jsonToVariantMap(step.value(QStringLiteral("params")).toObject());
            // 动态 id 替换："$lastSource"/"$lastDataset" 引用前序意图的产出
            for (auto it = intent.params.begin(); it != intent.params.end(); ++it)
            {
                if (it.value().toString() == QStringLiteral("$lastSource"))
                    it.value() = lastSource;
                else if (it.value().toString() == QStringLiteral("$lastDataset"))
                    it.value() = lastDataset;
            }
            const Result<IntentResult> r = application.host().execute(intent);
            if (!r)
            {
                fail(op, QStringLiteral("%1 失败: %2").arg(intent.op, r.error().message));
                continue;
            }
            const QVariantMap payload = r.value().payload.toMap();
            if (payload.contains(QStringLiteral("source")))
                lastSource = payload.value(QStringLiteral("source")).toInt();
            if (payload.contains(QStringLiteral("dataset")))
                lastDataset = payload.value(QStringLiteral("dataset")).toInt();
            ok(QStringLiteral("intent %1").arg(intent.op));
        }
        else if (op == QStringLiteral("view-action"))
        {
            const QString action = step.value(QStringLiteral("action")).toString();
            QVariant out;
            Result<QVariant> res(out);
            const bool hit = application.context().interactions().dispatchViewAction(
                action, jsonToVariantMap(step.value(QStringLiteral("params")).toObject()), res);
            if (!hit || !res.ok())
            {
                fail(op, QStringLiteral("%1: %2").arg(action, hit ? res.error().message : QStringLiteral("未注册")));
                continue;
            }
            QApplication::processEvents();
            ok(QStringLiteral("view-action %1").arg(action));
        }
        else if (op == QStringLiteral("activity-badge"))
        {
            window.binder()->setActivityBadge(
                step.value(QStringLiteral("id")).toString(),
                step.value(QStringLiteral("count")).toInt());
        }
        else if (op == QStringLiteral("panel"))
        {
            const bool created = window.context()->requestPanel(
                step.value(QStringLiteral("slot")).toString(),
                step.value(QStringLiteral("panel")).toString());
            if (!created)
            {
                fail(op, QStringLiteral("面板创建失败: %1").arg(step.value(QStringLiteral("panel")).toString()));
                continue;
            }
            QApplication::processEvents();
            ok(QStringLiteral("面板已打开 %1").arg(step.value(QStringLiteral("panel")).toString()));
        }
        else if (op == QStringLiteral("raise-panel"))
        {
            const QString area = step.value(QStringLiteral("area")).toString();
            const QString cls = step.value(QStringLiteral("class")).toString();
            bool raised = false;
            for (QWidget *p : window.binder()->panelWidgets(area))
            {
                // 类名可能带命名空间前缀，按后缀匹配
                if (QString::fromLatin1(p->metaObject()->className()).endsWith(cls)
                    || p->objectName() == cls)
                {
                    window.binder()->raisePanel(p);
                    raised = true;
                    break;
                }
            }
            if (!raised)
            {
                fail(op, QStringLiteral("dock 区 %1 未找到 %2").arg(area, cls));
                continue;
            }
            QApplication::processEvents();
            ok(QStringLiteral("面板置顶 %1").arg(cls));
        }
        else if (op == QStringLiteral("cursor"))
        {
            auto *ws = qobject_cast<PlotWorkspace *>(
                window.binder() ? window.binder()->centralContent() : nullptr);
            AxesView *pc = ws ? ws->activeCanvas() : nullptr;
            const double t = step.value(QStringLiteral("t")).toDouble();
            if (!pc)
            {
                fail(op, QStringLiteral("无活动画布"));
                continue;
            }
            pc->setCursorTime(t, true); // 固定读数
            pc->toggleAB();             // 生成 A/B（以游标为中心）
            pc->addBookmark(t, QStringLiteral("游标书签"));
            QApplication::processEvents();
            ok(QStringLiteral("游标 t=%1, ab=%2, bookmarks=%3")
                   .arg(t).arg(int(pc->abActive())).arg(int(pc->bookmarks().size())));
        }
        else if (op == QStringLiteral("hover-toolbar"))
        {
            auto *ws = qobject_cast<PlotWorkspace *>(
                window.binder() ? window.binder()->centralContent() : nullptr);
            if (!ws || !ws->activeCanvas())
            {
                fail(op, QStringLiteral("无活动画布"));
                continue;
            }
            ws->flashToolbar(false); // 浮出活动格工具栏且不自动收起，供截图
            QApplication::processEvents();
            ok(QStringLiteral("工具栏已浮出"));
        }
        else if (op == QStringLiteral("demo-live"))
        {
            // 注册演示 live 源（登记运行时实例，不真连接），呈现 live 卡片形态。
            DataSourceInfo info;
            const QString name = step.value(QStringLiteral("name")).toString(QStringLiteral("live-demo"));
            const bool paused = step.value(QStringLiteral("paused")).toBool(false);
            info.name = name;
            info.origin = paused ? QStringLiteral("udp://127.0.0.1:14550 (paused)")
                                 : QStringLiteral("udp://127.0.0.1:14550");
            info.kind = SourceKind::Live;
            info.state = paused ? SourceState::Paused : SourceState::Streaming;
            info.datasetIds = lastDataset >= 0 ? QVector<int>{lastDataset} : QVector<int>{};
            const int liveId = application.context().sources().add(info);
            application.context().setActiveSourceId(liveId);
            QApplication::processEvents();
            ok(QStringLiteral("演示 live 源 id=%1 name=%2 state=%3")
                   .arg(liveId).arg(name, paused ? QStringLiteral("Paused") : QStringLiteral("Streaming")));
        }
        else if (op == QStringLiteral("click") || op == QStringLiteral("dblclick"))
        {
            QWidget *w = window.findChild<QWidget *>(step.value(QStringLiteral("widget")).toString());
            if (!w)
            {
                fail(op, QStringLiteral("找不到控件: %1").arg(step.value(QStringLiteral("widget")).toString()));
                continue;
            }
            QPoint pos = w->rect().center();
            if (step.contains(QStringLiteral("x")) && step.contains(QStringLiteral("y")))
                pos = QPoint(step.value(QStringLiteral("x")).toInt(), step.value(QStringLiteral("y")).toInt());
            if (op == QStringLiteral("click"))
                QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, pos);
            else
                QTest::mouseDClick(w, Qt::LeftButton, Qt::NoModifier, pos);
            QApplication::processEvents();
            ok(QStringLiteral("%1 %2 @%3,%4").arg(op, w->objectName()).arg(pos.x()).arg(pos.y()));
        }
        else if (op == QStringLiteral("key"))
        {
            QWidget *w = window.findChild<QWidget *>(step.value(QStringLiteral("widget")).toString());
            if (!w)
            {
                fail(op, QStringLiteral("找不到控件: %1").arg(step.value(QStringLiteral("widget")).toString()));
                continue;
            }
            w->setFocus();
            QApplication::processEvents();
            if (step.contains(QStringLiteral("keys")))
                pressKeyCombo(w, step.value(QStringLiteral("keys")).toString());
            else if (step.contains(QStringLiteral("text")))
                QTest::keyClicks(w, step.value(QStringLiteral("text")).toString());
            QApplication::processEvents();
            ok(QStringLiteral("key %1").arg(step.value(QStringLiteral("keys")).toString(
                step.value(QStringLiteral("text")).toString())));
        }
        else if (op == QStringLiteral("type"))
        {
            QWidget *w = window.findChild<QWidget *>(step.value(QStringLiteral("widget")).toString());
            if (!w)
            {
                fail(op, QStringLiteral("找不到控件: %1").arg(step.value(QStringLiteral("widget")).toString()));
                continue;
            }
            w->setFocus();
            QApplication::processEvents();
            QTest::keyClicks(w, step.value(QStringLiteral("text")).toString());
            QApplication::processEvents();
            ok(QStringLiteral("type %1").arg(step.value(QStringLiteral("text")).toString()));
        }
        else if (op == QStringLiteral("action"))
        {
            const QString id = step.value(QStringLiteral("action")).toString();
            QAction *found = nullptr;
            const auto actions = window.findChildren<QAction *>();
            for (QAction *a : actions)
                if (a->objectName() == id)
                {
                    found = a;
                    break;
                }
            if (!found)
                for (QAction *a : actions)
                    if (a->text() == id)
                    {
                        found = a;
                        break;
                    }
            if (!found)
                for (QAction *a : actions)
                    if (a->data().toString() == id)
                    {
                        found = a;
                        break;
                    }
            if (!found)
            {
                fail(op, QStringLiteral("找不到动作: %1").arg(id));
                continue;
            }
            found->trigger();
            QApplication::processEvents();
            ok(QStringLiteral("action %1").arg(id));
        }
        else if (op == QStringLiteral("dialog"))
        {
            const QString name = step.value(QStringLiteral("name")).toString();
            const QString path = resolvePath(opt.outDir,
                                             step.value(QStringLiteral("shot")).toString(name + QStringLiteral(".png")));
            bool done = false;
            if (name == QStringLiteral("import"))
                done = dialogImport(application, step, path);
            else if (name == QStringLiteral("protocol"))
                done = dialogProtocol(step, path);
            else if (name == QStringLiteral("transform"))
                done = dialogTransform(application, step, path);
            else if (name == QStringLiteral("filter"))
                done = dialogFilter(application, window, step, path);
            else if (name == QStringLiteral("settings"))
                done = dialogSettings(application, window, step, path);
            else
            {
                fail(op, QStringLiteral("未知对话框: %1（现有 import/protocol/transform/filter/settings）").arg(name));
                continue;
            }
            if (!done)
                fail(op, name);
            else
                ok(QStringLiteral("dialog %1 -> %2").arg(name, path));
        }
        else if (op == QStringLiteral("dump"))
        {
            const QString path = resolvePath(opt.outDir, step.value(QStringLiteral("out")).toString(QStringLiteral("tree.txt")));
            QFile out(path);
            if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
            {
                fail(op, QStringLiteral("保存失败: %1").arg(path));
                continue;
            }
            QTextStream ts(&out);
            ts << "MainWindow\n";
            dumpWidgetTree(&window, ts, 1);
            ok(QStringLiteral("控件树 -> %1").arg(path));
        }
        else
        {
            fail(op, QStringLiteral("未知 op"));
        }
    }

    fprintf(stdout, failures == 0 ? "全部步骤通过\n" : "%d 个步骤失败\n", failures);
    return failures == 0 ? 0 : 1;
}
