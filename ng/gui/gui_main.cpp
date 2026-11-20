#include "MainWindow.h"
#include "ShellBinder.h"

#include "App.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QStatusBar>
#include <QCommandLineParser>
#include <QTimer>

// GUI 入口：与 CLI 入口并列的可执行程序。
//
// CLI（plotscope-ng）只链接 Qt Core，可在无显示环境跑批处理；
// GUI（plotscope-ng-gui）链接 Qt Widgets。二者共用同一套 app/host/core，
// 保证 headless 与交互行为完全一致（同一份模块、同一份意图语义）。
//
// 无人值守开关（对齐 PJ4 的 headless acceptance flags）：
//   --datafile <file>       启动载入数据文件，可重复（每个文件各走一次 data.load）
//   --autoplay              载入完成后自动开始播放（演示/性能剖析）
//   --exit-after-start[=秒] 窗口就绪并完成载入后退出；退出码 0=全部载入成功、
//                           1=有载入失败。默认等待 3 秒。布局恢复是同步的，
//                           show() 即就绪，无需 PJ4 那样的 settle 等待。
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
    QCommandLineOption exitAfterStartOpt(QStringLiteral("exit-after-start"),
                                         QStringLiteral("窗口就绪并完成载入后退出（退出码 0=成功/1=有失败）"),
                                         QStringLiteral("seconds"), QStringLiteral("3"));
    parser.addOption(exitAfterStartOpt);
    parser.process(app);

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

    window.show();

    // 自动播放：走与脚本/测试完全相同的 view action 通道（playback.play）。
    if (parser.isSet(autoplayOpt))
    {
        QTimer::singleShot(0, &window, [&application] {
            nextpilot::plotscope::foundation::Result<QVariant> out{ QVariant() };
            application.host().dispatchViewAction(QStringLiteral("playback.play"), {}, out);
        });
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
    return parser.isSet(exitAfterStartOpt) ? (loadFailures == 0 ? 0 : 1) : exitCode;
}
