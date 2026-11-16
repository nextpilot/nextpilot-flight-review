#include "ShellCommands.h"

#include "MainWindow.h"
#include "SettingsDialog.h"
#include "DiagnosticsDialog.h"

#include "Component.h"

#include <QAction>
#include <QCursor>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QShortcut>
#include <QWidget>

namespace shell
{

void registerCommands(MainWindow &win)
{
    using Ctx = ps::commands::CommandContext;

    auto describe = [&win](const QString &id, const QString &text, const QString &icon,
                           const QString &shortcut, bool checkable = false) {
        ps::commands::CommandSpec spec;
        spec.id = id;
        spec.text = text;
        spec.icon = icon;
        spec.shortcut = shortcut;
        spec.checkable = checkable;
        win.commands().describe(spec);
    };
    describe(QStringLiteral("file.open"), QStringLiteral("打开文件"), QStringLiteral("open"), QStringLiteral("Ctrl+O"));
    describe(QStringLiteral("file.importLayout"), QStringLiteral("导入布局"), QStringLiteral("import"), {});
    describe(QStringLiteral("file.saveLayout"), QStringLiteral("另存布局"), QStringLiteral("save"), QStringLiteral("Ctrl+Shift+S"));
    describe(QStringLiteral("file.exportSettings"), QStringLiteral("导出设置"), QStringLiteral("export"), {});
    describe(QStringLiteral("file.exportData"), QStringLiteral("导出数据"), QStringLiteral("export"), {});
    describe(QStringLiteral("file.screenshot"), QStringLiteral("截图"), QStringLiteral("camera"), {});
    describe(QStringLiteral("file.settings"), QStringLiteral("设置"), QStringLiteral("settings"), QStringLiteral("Ctrl+,"));
    describe(QStringLiteral("layout.save"), QStringLiteral("保存布局"), QStringLiteral("save"), QStringLiteral("Ctrl+S"));
    describe(QStringLiteral("layout.import"), QStringLiteral("打开布局"), QStringLiteral("open"), QStringLiteral("Ctrl+Shift+O"));
    describe(QStringLiteral("view.toggleDock"), QStringLiteral("显示/隐藏面板"), {}, {}, true);
    describe(QStringLiteral("help.about"), QStringLiteral("关于"), QStringLiteral("settings"), {});

    win.commands().add(QStringLiteral("file.open"), [&win](const Ctx &) {
        const QStringList fs = QFileDialog::getOpenFileNames(&win, QStringLiteral("打开文件"), {},
            QStringLiteral("飞行日志 (*.ulg *.bin *.csv *.asc *.blf);;全部 (*)"));
        for (const QString &f : fs) {
            ps::commands::CommandContext ctx;
            ctx.arg = f;
            win.commands().invoke(QStringLiteral("data.openPath"), ctx);
        }
    });
    win.commands().add(QStringLiteral("file.importLayout"), [&win](const Ctx &) {
        const QString f = QFileDialog::getOpenFileName(&win, QStringLiteral("导入布局"), {},
            QStringLiteral("PlotScope 布局 (*.psl)"));
        if (!f.isEmpty()) {
            ps::commands::CommandContext ctx;
            ctx.arg = f;
            win.commands().invoke(QStringLiteral("layout.import"), ctx);
        }
    });
    win.commands().add(QStringLiteral("file.saveLayout"), [&win](const Ctx &) {
        const QString f = QFileDialog::getSaveFileName(&win, QStringLiteral("导出布局"), {},
            QStringLiteral("PlotScope 布局 (*.psl)"));
        if (!f.isEmpty()) {
            ps::commands::CommandContext ctx;
            ctx.arg = f;
            win.commands().invoke(QStringLiteral("layout.save"), ctx);
        }
    });
    win.commands().add(QStringLiteral("file.exportSettings"), [&win](const Ctx &) {
        win.appendLog(QStringLiteral("导出设置：M1 接 YAML"));
    });
    win.commands().add(QStringLiteral("file.exportData"), [&win](const Ctx &) {
        win.appendLog(QStringLiteral("导出数据：M1 接 CSV/NDF"));
    });
    win.commands().add(QStringLiteral("file.screenshot"), [&win](const Ctx &) {
        QWidget *page = win.pages() ? win.pages()->currentWidget() : nullptr;
        if (!page)
            return;
        const QString f = QFileDialog::getSaveFileName(&win, QStringLiteral("截图另存为"), {},
            QStringLiteral("PNG (*.png)"));
        if (f.isEmpty())
            return;
        if (page->grab().save(f))
            win.appendLog(QStringLiteral("已截图：%1").arg(f));
    });

    win.commands().add(QStringLiteral("file.settings"), [&win](const Ctx &) {
        SettingsDialog dlg(&win);
        dlg.exec();
    });

    win.commands().add(QStringLiteral("help.about"), [&win](const Ctx &) {
        DiagnosticsDialog dlg(&win, win.kernelInfo(), win.extensions().extensions(),
                              win.extensions().errors());
        dlg.exec();
    });

    win.commands().add(QStringLiteral("view.toggleDock"), [&win](const Ctx &ctx) {
        if (auto *d = win.findChild<QDockWidget *>(ctx.arg))
            d->setVisible(!d->isVisible());
    });
}


void bindSession(MainWindow &win)
{
    win.commands().setInvokeHook([&win](const QString &id) {
        if (id == QLatin1String("layout.save") || id == QLatin1String("layout.import"))
        {
            win.session().markClean();
            win.refreshTitle();
            return;
        }
        if (id == QLatin1String("plot.refreshTree") || id == QLatin1String("plot.setPlayhead") ||
            id == QLatin1String("data.recent") || id == QLatin1String("data.quickOpen") ||
            id == QLatin1String("data.clearCache") || id == QLatin1String("app.beforeClose") ||
            id.startsWith(QLatin1String("view.")))
            return;
        if (id.startsWith(QLatin1String("data.")) || id.startsWith(QLatin1String("plot.")) ||
            id.startsWith(QLatin1String("page.")))
        {
            win.session().markDirty();
            win.refreshTitle();
        }
    });
}


} // namespace shell
