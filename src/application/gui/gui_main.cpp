#include "MainWindow.h"
#include "ShellBinder.h"
#include "ExtensionActivationService.h"
#include "BuiltinViewProviders.h"  // gui 侧：壳内置工作台视图 provider 注册半（复审必修 2）
#include "BuiltinWorkbenchViews.h" // 装配层：壳内置工作台视图声明半（owner core.workbench，纯数据）
#include "DataViewFactories.h"     // data 模块视图装配（③）：datasetTree view + data.table editor
#include "PlotViewFactories.h"     // plot 模块视图装配（A2c）：plot.properties view + instrument/workspace editor
#include "ScriptViewFactories.h"   // script 模块视图装配（A2c）：files/console/debug view + editor/batchSummary editor
#include "AutomotiveViewFactories.h" // automotive 领域视图装配（A2c）：canBrowser view
#ifdef PS_HAS_REPORT_GUI
#include "ReportExporter.h" // 装配层：注册 report.export viewAction（业务在 report 模块）
#endif

#include "App.h"
#include "ProductSpec.h"
#include "diagnostics.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPixmap>
#include <QStandardPaths>
#include <QStatusBar>
#include <QCommandLineParser>
#include <QTimer>
#include <WorkspaceDataConfig.h>

#ifdef _WIN32
#include <windows.h>
#endif

// GUI 入口：与 CLI 入口并列的可执行程序。
//
// CLI（plotscope）只链接 Qt Core，可在无显示环境跑批处理；
// GUI（plotscope-gui）链接 Qt Widgets。二者共用同一套 app/host/core，
// 保证 headless 与交互行为完全一致（同一份模块、同一份意图语义）。
//
// 无人值守开关：
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
namespace {
// qPrintable 走系统 ANSI，和源码里的 UTF-8 字面量混流会乱码，这里统一按 UTF-8 输出
const char *u8(const QString &s)
{
    return s.toUtf8().constData();
}

// 完整产品的身份清单（装配层唯一事实源；壳代码不出现产品常量）。
// moduleIds 为声明式清单：当前装配仍由组合根链接全部模块，后续按清单过滤装载。
nextpilot::plotscope::registry::ProductSpec plotscopeProductSpec()
{
    // 布局与模块清单的唯一事实源在 libs 层（测试亦复用同一份，避免重复常量）。
    return nextpilot::plotscope::registry::plotScopeProductSpec();
}
}

namespace {
// 装配失败诊断（第③步必带：provider/声明注册失败不许静默）：AppContext 结构化
// error 通道 + stderr，同一处出口；失败不 abort——build 内缺失面会走校验器补报。
void reportAssemblyFailure(nextpilot::plotscope::context::AppContext &appCtx, const char *what)
{
    appCtx.appendMessage(QStringLiteral("error"), QStringLiteral("builtin-views"),
                         QStringLiteral("启动期视图装配失败: %1").arg(what));
    fprintf(stderr, "[builtin-views] 启动期视图装配失败: %s\n", what);
}
} // namespace

int main(int argc, char *argv[])
{
#ifdef _WIN32
    // 控制台代码页默认 936，切到 UTF-8 才能正常显示中文
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    const nextpilot::plotscope::registry::ProductSpec productSpec = plotscopeProductSpec();

    QApplication app(argc, argv);
    QApplication::setApplicationName(productSpec.id);
    QApplication::setApplicationVersion(productSpec.version);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("%1 —— 数据绘图分析软件（图形界面）").arg(productSpec.displayName));
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
    QCommandLineOption settingsOpt(QStringLiteral("settings"),
                                   QStringLiteral("设置文件路径（默认 <AppData>/%1/settings.json）")
                                       .arg(productSpec.id),
                                   QStringLiteral("file"));
    parser.addOption(settingsOpt);
    parser.process(app);

    // 诊断模式抑制模块提示，保证 stdout 是纯 JSON。
    if (parser.isSet(dumpDiagOpt))
        qputenv("PLOTSCOPE_QUIET_STDOUT", "1");

    // 设置落盘：App 作用域偏好（主题 / 时间轴刻度 / 导出与重算上限…）跨会话保留。
    // 之前 GUI 全部用内存模式——"偏好设置"改完重启就丢，等于假开关。
    // 路径取 <AppData>/plotscope/settings.json（AppDataLocation 已含 applicationName）；
    // --settings 可覆盖，便于测试与便携部署，不污染真实配置。
    QString settingsPath = parser.value(settingsOpt);
    if (settingsPath.isEmpty())
    {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (!dir.isEmpty())
        {
            // SettingsStore 用 QSaveFile 原子写，但不会自己建目录（首次运行会失败）。
            QDir().mkpath(dir);
            settingsPath = dir + QStringLiteral("/settings.json");
        }
    }

    nextpilot::plotscope::app::App application(settingsPath);
    // 壳内置工作台视图（A2b 笔2b 第②步返工）：先声明（assembly，纯数据）、
    // 后注册 provider（gui 侧）——两半都**先于** window.build：build 内
    // defaultPanels 走 views 声明与 provider。第③步必带：任一注册失败写启动期
    // error 诊断（AppContext + stderr），不许静默。
    if (!nextpilot::plotscope::assembly::declareBuiltinWorkbenchViews(application.host().services()))
        reportAssemblyFailure(application.context(), "declareBuiltinWorkbenchViews");
    if (!nextpilot::plotscope::gui::installBuiltinWorkbenchViewProviders(application.host().services()))
        reportAssemblyFailure(application.context(), "installBuiltinWorkbenchViewProviders");
    if (!nextpilot::plotscope::data::installDataViewFactories(application.host().services(), &application.context(),
                                                              &application.host()))
        reportAssemblyFailure(application.context(), "installDataViewFactories");
    if (!nextpilot::plotscope::plot::installPlotViewFactories(application.host().services(), &application.context(),
                                                              &application.host()))
        reportAssemblyFailure(application.context(), "installPlotViewFactories");
    if (!nextpilot::plotscope::script::installScriptViewFactories(application.host().services(), &application.context(),
                                                                  &application.host()))
        reportAssemblyFailure(application.context(), "installScriptViewFactories");
    if (!nextpilot::plotscope::ext::automotive::installAutomotiveViewFactories(
            application.host().services(), &application.context(), &application.host()))
        reportAssemblyFailure(application.context(), "installAutomotiveViewFactories");
#ifdef PS_HAS_REPORT_GUI
    // 组合根注册 report.export 的 host；report 模块在 initialize 安装、onDetach 卸载。
    // 必须在 window.build（内部 host.activate）之前注册，模块 initialize 才取得到。
    // 析构顺序是这里的硬约束：host 先于 application 声明、先于它析构，而 ~App 里还会
    // deactivateAll —— 那时模块 onDetach 会对着已析构的 host 做虚调用（Debug 下
    // "pure virtual method called" 直接 abort）。故在 host 之后声明一个守卫，
    // 让本块退出时先拆模块、再让 host 死亡。
    nextpilot::plotscope::gui::ReportExportHost reportExportHost(application.context());
    application.host().services()->registerSingleton(
        QStringLiteral("report.exportHost"), &reportExportHost, QStringLiteral("assembly"), 1,
        nextpilot::plotscope::instantiation::ServiceTypeTag<nextpilot::plotscope::report::IReportExportHost>::value());
    struct DeactivateBeforeHostDeath
    {
        App &app;
        ~DeactivateBeforeHostDeath() { app.host().deactivateAll(); }
    } deactivateGuard{application};
#endif

    nextpilot::plotscope::gui::MainWindow window(application.context(), productSpec);
    window.build(application.host());

    // A8b-M1：把扩展 contributes 的命令接进命令面板。MainWindow 零改动——
    // 装配层用 public getter 拿 CommandRegistry*，激活服务只按声明摆放、不解释 id。
    // 裁定 52441633 §四：只传 AppContext，扩展注册表的获取留在装配层，
    // gui 目录不再出现扩展注册表的获取路径（§9 e) 判据）。
    nextpilot::plotscope::assembly::ExtensionActivationService extensionActivation(
        application.context());
    extensionActivation.bind(window.commandRegistry(), application.host());

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

    // 启动即最大化：PlotScope 的用法是长时间盯着多面板（数据集树 + 画布 + 属性），
    // 1280×800 默认尺寸会让画布被 dock 挤掉一半；无持久化窗口几何，直接最大化更符合使用预期。
    window.showMaximized();

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
                fprintf(stderr, "[diagnostics] write failed: %s\n", u8(outPath));
            }
        }
    }

    // 截屏验收：延迟 grab 全窗口存 PNG，失败退出码 2。
    if (parser.isSet(screenshotOpt))
    {
        const QString pngPath = parser.value(screenshotOutOpt);
        bool ok = false;
        const int seconds = parser.value(screenshotDelayOpt).toInt(&ok);
        const int waitMs = (ok && seconds >= 0 ? seconds : 2) * 1000;

        if (pngPath.isEmpty())
        {
            fprintf(stderr, "[screenshot] missing --screenshot-output <png>\n");
            QTimer::singleShot(0, &window, [] { QApplication::exit(2); });
        }
        else
        {
            QTimer::singleShot(waitMs, &window, [&window, pngPath] {
                const QPixmap shot = window.grab();
                if (shot.isNull() || !shot.save(pngPath))
                {
                    fprintf(stderr, "[screenshot] capture or save failed: %s\n", u8(pngPath));
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
