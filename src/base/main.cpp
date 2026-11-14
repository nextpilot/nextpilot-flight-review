// PlotScope 入口。Qt Widgets 单线程 UI（D1 / D1b / D1c），数据面全部走 ps-abi。
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStringList>

#include <cstdio>
#include <QStandardPaths>

#include "host/extension/Component.h"
#include "host/extension/DataInputRegistry.h"
#include "shell/Theme.h"

#include "ps_kernel.h"
#include "shell/MainWindow.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PlotScope"));
    QApplication::setOrganizationName(QStringLiteral("NextPilot"));

    // 全局主题：样式表 + 深色 palette 一次装好，所有窗口共用
    theme::applyTo(app);

    // 版本闸门：宿主只认自己声明的 ABI，不匹配直接拒启（D9）
    if (ps_abi_version() != PS_ABI_VERSION)
    {
        qFatal("ps-abi 版本不匹配：kernel=%u host=%u", ps_abi_version(), PS_ABI_VERSION);
    }

    // 动态扩展：安装目录 + 用户目录 + 环境变量，app 不认具体格式
    QStringList extensionDirs;
    extensionDirs << QCoreApplication::applicationDirPath() + QStringLiteral("/extensions");
    extensionDirs << QCoreApplication::applicationDirPath() + QStringLiteral("/../lib/plotscope/extensions");
    extensionDirs << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/extensions");
    const QString envPath = qEnvironmentVariable("PLOTSCOPE_EXTENSION_PATH");
    if (!envPath.isEmpty())
        extensionDirs << envPath.split(QDir::listSeparator(), Qt::SkipEmptyParts);
    for (const QString &e : DataInputRegistry::instance().scanExtensions(extensionDirs))
        qWarning("[data_input] %s", qPrintable(e));

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
    if (argc > 1)
    {
        for (int i = 1; i < argc; ++i)
        {
            const QString arg = QString::fromLocal8Bit(argv[i]);
            if (arg.endsWith(QStringLiteral(".psl"), Qt::CaseInsensitive))
                win.importLayoutFrom(arg);
            else
                win.loadFile(arg);
        }
    }
    else if (QSettings(QStringLiteral("NextPilot"), QStringLiteral("PlotScope"))
                 .value(QStringLiteral("restoreLayout"), false).toBool())
    {
        const QString last = win.defaultLayoutPath();
        if (QFile::exists(last))
            win.importLayoutFrom(last);
    }

    return app.exec();
}
