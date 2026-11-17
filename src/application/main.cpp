// PlotScope 入口。Qt Widgets 单线程 UI（D1 / D1b / D1c），数据面全部走 ps-abi。
#include <QApplication>
#include <QFile>
#include <QStandardPaths>
#include <QStringList>

#include <cstdio>

#include "Component.h"
#include "DataSourceRegistry.h"
#include "DynamicExtensionRegistry.h"
#include "ThemeManager.h"
#include "SettingsManager.h"

#include "ps_kernel.h"
#include "MainWindow.h"

// --run 走无 UI 数据面
#include "ExtensionHost.h"
#include "HostDataService.h"
#include "datasource/HostDataBridge.h"
#include "scripting/ScriptEngine.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PlotScope"));
    QApplication::setOrganizationName(QStringLiteral("NextPilot"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    // 全局主题：样式表 + 深色 palette 一次装好，所有窗口共用
    ThemeManager::applyTo(app);

    // 版本闸门：宿主只认自己声明的 ABI，不匹配直接拒启（D9）
    if (ps_abi_version() != PS_ABI_VERSION)
    {
        qFatal("ps-abi 版本不匹配：kernel=%u host=%u", ps_abi_version(), PS_ABI_VERSION);
    }

    // 动态扩展：目录扫描归 ExtensionManager，注册归各扩展点。
    ExtensionManager::scanStandardLocations();
    QStringList extensionErrors = DataSourceRegistry::instance().registerLoadedExtensions();
    extensionErrors += ps::components::registerDynamicExtensions();
    for (const QString &e : extensionErrors)
        qWarning("[extension] %s", qPrintable(e));

    if (QCoreApplication::arguments().contains(QStringLiteral("--list-extensions")))
    {
        const auto &entries = ps::components::ExtensionRegistry::instance().extensions();
        for (const auto &entry : entries)
        {
            const auto &d = entry.descriptor;
            printf("%s\t%s\t%s\t%s\n", qPrintable(d.id),
                   d.type == ps::components::ExtensionType::Builtin ? "builtin" : "dynamic",
                   qPrintable(d.version), qPrintable(d.libraryPath));
        }
        return 0;
    }

    // --run <script.lua>：无 UI 跑脚本，退出码即结论（CI 入口）
    const QStringList cliArgs = QCoreApplication::arguments();
    const int runIndex = cliArgs.indexOf(QStringLiteral("--run"));
    if (runIndex > 0 && runIndex + 1 < cliArgs.size())
    {
        const QString scriptPath = cliArgs.at(runIndex + 1);
        QFile f(scriptPath);
        if (!f.open(QIODevice::ReadOnly))
        {
            fprintf(stderr, "无法读取脚本：%s\n", qPrintable(scriptPath));
            return 2;
        }
        const QString source = QString::fromUtf8(f.readAll());

        ps::data_source::SourceStore store;
        ps::data_source::HostDataBridge bridge(store);
        ps::host::setHostService(&bridge);

        // CLI 没有 MainWindow，内置组件的 contribute 要自己激活一遍，
        // 否则加载器注册表是空的，ps.app.load 找不到 csv/ulog 等逻辑。
        ps::components::HostContext cliCtx;
        cliCtx.log = [](const QString &text) { fprintf(stderr, "%s\n", qPrintable(text)); };
        cliCtx.configDir = [] {
            return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        };
        ps::components::ExtensionHost cliHost;
        cliHost.activate(cliCtx);
        for (const QString &e : cliHost.errors())
            fprintf(stderr, "[extension] %s\n", qPrintable(e));

        const ScriptResult r =
            ScriptEngine::run(source, QVector<ScriptSeries>(), QMap<QString, double>(),
                              QStringLiteral("lua"), false);
        ps::host::setHostService(nullptr);
        cliHost.deactivate();
        if (!r.ok)
        {
            fprintf(stderr, "%s\n", qPrintable(r.error));
            return 1;
        }
        return 0;
    }

    MainWindow win;
    win.show();

    // 命令行带文件时直接加载：plotscope_app flight.csv / layout.psl
    bool restoreLayout = false;
    for (int i = 1; i < argc; ++i)
    {
        if (QString::fromLocal8Bit(argv[i]).endsWith(QStringLiteral(".psl"), Qt::CaseInsensitive))
            restoreLayout = true;
    }
    if (argc <= 1 && SettingsManager::instance().restoreLayout())
    {
        const QString last = win.session().layoutPath();
        restoreLayout = !last.isEmpty() && QFile::exists(last);
    }
    if (!restoreLayout)
        win.session().restoreWindowState(&win);

    if (argc > 1)
    {
        for (int i = 1; i < argc; ++i)
        {
            const QString arg = QString::fromLocal8Bit(argv[i]);
            ps::commands::CommandContext ctx;
            ctx.arg = arg;
            if (arg.endsWith(QStringLiteral(".psl"), Qt::CaseInsensitive))
                win.commands().invoke(QStringLiteral("layout.import"), ctx);
            else
                win.commands().invoke(QStringLiteral("data.openPath"), ctx);
        }
    }
    else if (SettingsManager::instance().restoreLayout())
    {
        const QString last = win.session().layoutPath();
        if (!last.isEmpty() && QFile::exists(last)) {
            ps::commands::CommandContext ctx;
            ctx.arg = last;
            win.commands().invoke(QStringLiteral("layout.import"), ctx);
        }
    }

    return app.exec();
}
