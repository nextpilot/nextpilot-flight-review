#include "MainWindow.h"
#include "ShellBinder.h"

#include "App.h"
#include "diagnostics.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPixmap>
#include <QStatusBar>
#include <QCommandLineParser>
#include <QTimer>
#include <WorkspaceDataConfig.h>

// GUI 入口：与 CLI 入口并列的可执行程序。
//
// CLI（plotscope-ng）只链接 Qt Core，可在无显示环境跑批处理；
// GUI（plotscope-ng-gui）链接 Qt Widgets。二者共用同一套 app/host/core，
// 保证 headless 与交互行为完全一致（同一份模块、同一份意图语义）。
//
// 无人值守开关（对齐 PJ4 的 headless acceptance flags）：
//   --datafile <file>       启动载入数据文件，可重复（每个文件各走一次 data.load）
//   --autoplay              载入完成后自动开始播放（演示/性能剖析）
//   --layout <file.json>    启动恢复工作区配方（data.workspace.save 产物）：
//                           导入 settings 后重放 data.workspace.restore
//   --screenshot            延迟截屏后退出；需配合 --screenshot-output <png>；
//                           延迟秒数用 --screenshot-delay（默认 2）；
//                           退出码 0=截图成功保存、2=未指定输出/截屏/保存失败
//   --exit-after-start[=秒] 窗口就绪并完成载入后退出；退出码 0=全部载入成功、
//                           1=有载入失败。默认等待 3 秒。布局恢复是同步的，
//                           show() 即就绪，无需 PJ4 那样的 settle 等待。
//   --dump-diagnostics[=f]  就绪后输出诊断 JSON（无值走 stdout，有值写文件）
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("plotscope-ng"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("PlotScope NG —— 数据绘图分析软件（图形界面）"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption openOpt(QStringLiteral("open"),
                                QStringLiteral("启动时载入数据文件"), QStringLiteral("file"));
    parser.addOption(openOpt);
    QCommandLineOption datafileOpt(QStringList{ QStringLiteral("d"), QStringLiteral("datafile") },
                                   QStringLiteral("启动载入数据文件（可重复，对齐 PJ4）"),
                                   QStringLiteral("file"));
    parser.addOption(datafileOpt);
    QCommandLineOption autoplayOpt(QStringLiteral("autoplay"),
                                   QStringLiteral("载入完成后自动开始播放"));
    parser.addOption(autoplayOpt);
    QCommandLineOption layoutOpt(QStringLiteral("layout"),
                                 QStringLiteral("启动恢复工作区配方 JSON（data.workspace.save 产物）"),
                                 QStringLiteral("file"));
    parser.addOption(layoutOpt);
    QCommandLineOption screenshotOpt(QStringLiteral("screenshot"),
                                     QStringLiteral("延迟截屏后退出（需 --screenshot-output，缺图/失败退出码 2）"));
    parser.addOption(screenshotOpt);
    QCommandLineOption screenshotDelayOpt(QStringLiteral("screenshot-delay"),
                                          QStringLiteral("截屏前等待秒数（默认 2，等数据载入渲染）"),
                                          QStringLiteral("seconds"), QStringLiteral("2"));
    parser.addOption(screenshotDelayOpt);
    QCommandLineOption screenshotOutOpt(QStringLiteral("screenshot-output"),
                                        QStringLiteral("截图 PNG 输出路径"), QStringLiteral("file"));
    parser.addOption(screenshotOutOpt);
    QCommandLineOption dumpDiagOpt(QStringLiteral("dump-diagnostics"),
                                   QStringLiteral("就绪后输出诊断 JSON（无值走 stdout，有值写文件）"),
                                   QStringLiteral("file"));
    parser.addOption(dumpDiagOpt);
    QCommandLineOption exitAfterStartOpt(QStringLiteral("exit-after-start"),
                                         QStringLiteral("窗口就绪并完成载入后退出（退出码 0=成功/1=有失败）"),
                                         QStringLiteral("seconds"), QStringLiteral("3"));
    parser.addOption(exitAfterStartOpt);
    parser.process(app);

    // 诊断模式抑制模块提示，保证 stdout 是纯 JSON。
    if (parser.isSet(dumpDiagOpt))
        qputenv("PLOTSCONG_QUIET_STDOUT", "1");

    nextpilot::plotscope::app::App application;
    nextpilot::plotscope::gui::MainWindow window(application.context());
    window.build(application.host());

    int loadFailures = 0;
    auto loadOneReported = [&](const QString &path) {
        nextpilot::plotscope::intent::Intent intent;
        intent.op = QStringLiteral("data.load");
        intent.params[QStringLiteral("path")] = path;
        const auto result = application.host().execute(intent);
        if (!result)
        {
            ++loadFailures;
            window.statusBar()->showMessage(QStringLiteral("载入失败: %1")
                                                .arg(result.error().message));
        }
    };

    // 兼容旧单文件开关；--datafile 可重复。
    if (parser.isSet(openOpt))
        loadOneReported(parser.value(openOpt));
    const QStringList datafiles = parser.values(datafileOpt);
    for (const QString &f : datafiles)
        loadOneReported(f);

    // 布局恢复：读配方 JSON → 导入 settings → 重放 data.workspace.restore。
    // 单个源失败不阻断其余（文件可能已移动），但整体失败计入退出码。
    if (parser.isSet(layoutOpt))
    {
        const QString layoutPath = parser.value(layoutOpt);
        QFile f(layoutPath);
        if (!f.open(QIODevice::ReadOnly))
        {
            ++loadFailures;
            window.statusBar()->showMessage(QStringLiteral("布局文件打不开: %1").arg(layoutPath));
        }
        else
        {
            QJsonParseError perr;
            const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
            if (doc.isNull())
            {
                ++loadFailures;
                window.statusBar()->showMessage(QStringLiteral("布局 JSON 解析失败: %1")
                                                    .arg(perr.errorString()));
            }
            else
            {
                nextpilot::plotscope::context::WorkspaceDataConfig cfg =
                    nextpilot::plotscope::context::WorkspaceDataConfig::fromJson(doc.object());
                QString err;
                if (!cfg.save(application.context().settings(), &err))
                {
                    ++loadFailures;
                    window.statusBar()->showMessage(QStringLiteral("布局写入失败: %1").arg(err));
                }
                else
                {
                    nextpilot::plotscope::intent::Intent restore;
                    restore.op = QStringLiteral("data.workspace.restore");
                    const auto rr = application.host().execute(restore);
                    if (!rr)
                    {
                        ++loadFailures;
                        window.statusBar()->showMessage(QStringLiteral("布局恢复失败: %1")
                                                            .arg(rr.error().message));
                    }
                }
            }
        }
    }

    window.show();

    // 自动播放：走与脚本/测试完全相同的 view action 通道（playback.play）。
    if (parser.isSet(autoplayOpt))
    {
        QTimer::singleShot(0, &window, [&application] {
            nextpilot::plotscope::foundation::Result<QVariant> out{ QVariant() };
            application.host().dispatchViewAction(QStringLiteral("playback.play"), {}, out);
        });
    }

    // 诊断输出：等 exit-after-start 的退出定时器统一收尾；这里只负责写。
    int diagFailures = 0;
    if (parser.isSet(dumpDiagOpt))
    {
        const QJsonDocument doc(nextpilot::plotscope::app::diag::collect(application));
        const QString outPath = parser.value(dumpDiagOpt);
        if (outPath.isEmpty())
        {
            fprintf(stdout, "%s\n",
                    QString::fromUtf8(doc.toJson(QJsonDocument::Indented)).toUtf8().constData());
        }
        else
        {
            QFile out(outPath);
            if (!out.open(QIODevice::WriteOnly | QIODevice::Text)
                || out.write(doc.toJson(QJsonDocument::Indented)) < 0)
            {
                ++diagFailures;
                fprintf(stderr, "[diagnostics] 写入失败: %s\n", qPrintable(outPath));
            }
        }
    }

    // 截屏验收（对齐 PJ4 --screenshot/--screenshot-delay）：延迟 grab 全窗口存 PNG，失败退出码 2。
    if (parser.isSet(screenshotOpt))
    {
        const QString pngPath = parser.value(screenshotOutOpt);
        bool ok = false;
        const int seconds = parser.value(screenshotDelayOpt).toInt(&ok);
        const int waitMs = (ok && seconds >= 0 ? seconds : 2) * 1000;

        if (pngPath.isEmpty())
        {
            fprintf(stderr, "[screenshot] 缺少 --screenshot-output <png>\n");
            QTimer::singleShot(0, &window, [] { QApplication::exit(2); });
        }
        else
        {
            QTimer::singleShot(waitMs, &window, [&window, pngPath] {
                const QPixmap shot = window.grab();
                if (shot.isNull() || !shot.save(pngPath))
                {
                    fprintf(stderr, "[screenshot] 截屏或保存失败: %s\n", qPrintable(pngPath));
                    QApplication::exit(2);
                }
                else
                {
                    QApplication::quit();
                }
            });
        }
    }

    // 无人值守验收：就绪后退出，退出码承载载入结果（CI/截图脚本用）。
    if (parser.isSet(exitAfterStartOpt))
    {
        bool ok = false;
        const int seconds = parser.value(exitAfterStartOpt).toInt(&ok);
        const int waitMs = (ok && seconds >= 0 ? seconds : 3) * 1000;
        QTimer::singleShot(waitMs, &window, [] { QApplication::quit(); });
    }

    const int exitCode = app.exec();
    // 截屏退出码只在 screenshot 模式且未与验收模式组合时单独生效；
    // 组合场景（screenshot + exit-after-start）以载入结果为准。
    if (parser.isSet(screenshotOpt) && !parser.isSet(exitAfterStartOpt))
        return diagFailures == 0 ? exitCode : 2;
    if (parser.isSet(exitAfterStartOpt) || parser.isSet(layoutOpt))
        return loadFailures == 0 && diagFailures == 0 ? 0 : 1;
    return exitCode;
}
