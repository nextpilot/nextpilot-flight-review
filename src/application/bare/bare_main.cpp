// ps_shell_bare —— 裸壳独立产物（契约 §19/§20：第二产品可复用的硬证据）。
//
// 与完整产品 plotscope-gui 的区别只在**装配**：这里不链接、不注册任何
// ps_module_* / ps_toolbox_* / ps_parser_*，只把壳骨架（ActivityBar /
// PrimarySidebar / Editor / Panel / StatusBar）用一份空模块清单的
// ProductSpec 起起来。壳代码本身与完整产品共用同一份 ps_gui，零产品分支。
//
// 用法：
//   ps_shell_bare            正常开窗（供人工查看空态）
//   ps_shell_bare --smoke    建窗、跑一轮事件循环后退出；
//                            warning/critical/fatal 计数，任一非零即非零退出。

#include "MainWindow.h"
#include "AppContext.h"
#include "RuntimeHost.h"
#include "ProductSpec.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QTimer>

#include <cstdio>
#include <cstdlib>

namespace
{

// 第二份 ProductSpec：id=bare，空模块清单、空默认布局（面板区走空态占位）。
// 这份实例只属于装配层（裸壳入口），壳代码不认识 "bare" 这个名字。
nextpilot::plotscope::registry::ProductSpec bareProductSpec()
{
    using nextpilot::plotscope::registry::ProductSpec;
    ProductSpec spec;
    spec.id = QStringLiteral("bare");
    spec.displayName = QStringLiteral("Bare Shell");
    spec.version = QStringLiteral("0.1.0");
    // defaultPanels 留空：不请求任何业务面板；moduleIds 留空：不装载任何模块。
    return spec;
}

int g_warnings = 0;
int g_criticals = 0;

// 冒烟期消息探针：保留 stderr 输出，同时计数供退出码判定。
// 重点盯：悬空 QObject::connect、迭代空 QHash、provider 空指针。
void smokeMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    switch (type)
    {
    case QtWarningMsg:
        ++g_warnings;
        std::fprintf(stderr, "[bare-smoke][warn] %s\n", msg.toUtf8().constData());
        break;
    case QtCriticalMsg:
        ++g_criticals;
        std::fprintf(stderr, "[bare-smoke][crit] %s\n", msg.toUtf8().constData());
        break;
    case QtFatalMsg:
        std::fprintf(stderr, "[bare-smoke][fatal] %s\n", msg.toUtf8().constData());
        std::abort();
    default:
        std::fprintf(stderr, "[bare-smoke] %s\n", msg.toUtf8().constData());
        break;
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    const nextpilot::plotscope::registry::ProductSpec spec = bareProductSpec();
    QApplication::setApplicationName(spec.id);
    QApplication::setApplicationVersion(spec.version);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("%1（裸壳独立产物：零模块 / 零 toolbox / 零 parser）").arg(spec.displayName));
    const QCommandLineOption smokeOpt(
        QStringLiteral("smoke"),
        QStringLiteral("建窗、跑一轮事件循环后退出；出现 warning/critical/fatal 即非零退出"));
    parser.addOption(smokeOpt);
    parser.process(app);

    const bool smoke = parser.isSet(smokeOpt);
    if (smoke)
        qInstallMessageHandler(smokeMessageHandler);

    // 直接用 libs 层运行态：不走 ps_app（ps_app 会装配完整产品的模块与知识包）。
    nextpilot::plotscope::context::AppContext ctx; // 空路径 = 内存设置，不落盘
    nextpilot::plotscope::host::RuntimeHost host(ctx);

    nextpilot::plotscope::gui::MainWindow window(ctx, spec);
    window.build(host);
    window.show();

    if (smoke)
    {
        // 一轮事件循环：让 dock/ribbon/面板工厂补登、首帧布局跑完，再退出。
        QTimer::singleShot(0, &window, [&app]() {
            app.processEvents();
            app.quit();
        });
    }

    const int rc = app.exec();
    if (smoke)
    {
        std::fprintf(stderr, "[bare-smoke] warnings=%d criticals=%d rc=%d\n",
                     g_warnings, g_criticals, rc);
        // 验收 18/20：裸壳冒烟必须干净——无 qFatal、无 Qt 警告、退出码 0。
        if (rc != 0 || g_criticals > 0 || g_warnings > 0)
            return 2;
    }
    return rc;
}
