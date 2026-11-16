// PlotScope 入口。Qt Widgets 单线程 UI（D1 / D1b / D1c），数据面全部走 ps-abi。
#include <QApplication>
#include <QFile>
#include <QStringList>

#include <cstdio>

#include "Component.h"
#include "DataSourceRegistry.h"
#include "DynamicExtensionRegistry.h"
#include "ThemeManager.h"
#include "SessionManager.h"
#include "SettingsManager.h"

#include "ps_kernel.h"
#include "MainWindow.h"

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
        restoreLayout = QFile::exists(SessionManager::defaultLayoutPath());
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
        const QString last = SessionManager::defaultLayoutPath();
        if (QFile::exists(last)) {
            ps::commands::CommandContext ctx;
            ctx.arg = last;
            win.commands().invoke(QStringLiteral("layout.import"), ctx);
        }
    }

    return app.exec();
}
