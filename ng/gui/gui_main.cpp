#include "DataManagerPanel.h"
#include "DockManager.h"
#include "MainWindow.h"
#include "PlotCanvas.h"
#include "RibbonBar.h"

#include "App.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QStatusBar>
#include <QCommandLineParser>

// GUI 入口：与 CLI 入口并列的可执行程序。
//
// CLI（plotscope-ng）只链接 Qt Core，可在无显示环境跑批处理；
// GUI（plotscope-ng-gui）链接 Qt Widgets。二者共用同一套 app/host/core，
// 保证 headless 与交互行为完全一致（同一份模块、同一份意图语义）。
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
    parser.process(app);

    ng::app::App application;
    ng::gui::MainWindow window(application.data());
    window.build(application.modules());

    // 启动即载文件：走与 CLI 完全相同的数据.load 意图路径。
    if (parser.isSet(openOpt))
    {
        ng::core::Intent intent;
        intent.op = QStringLiteral("data.load");
        intent.params[QStringLiteral("path")] = parser.value(openOpt);
        const auto result = application.modules().execute(intent);
        if (!result)
            window.statusBar()->showMessage(QStringLiteral("载入失败: %1")
                                                 .arg(result.error().message));
    }

    window.show();
    return app.exec();
}
