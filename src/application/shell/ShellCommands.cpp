#include "ShellCommands.h"

#include "MainWindow.h"
#include "DiagnosticsDialog.h"

#include "Component.h"

#include <QAction>
#include <QCursor>
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
                           const QString &shortcut, bool checkable = false, bool enabled = true) {
        ps::commands::CommandSpec spec;
        spec.id = id;
        spec.text = text;
        spec.icon = icon;
        spec.shortcut = shortcut;
        spec.checkable = checkable;
        if (!enabled)
            spec.enabled = [](const Ctx &) { return false; };
        win.commands().describe(spec);
    };
    describe(QStringLiteral("file.open"), QStringLiteral("打开文件"), QStringLiteral("open"), QStringLiteral("Ctrl+O"));
    describe(QStringLiteral("file.importLayout"), QStringLiteral("导入布局"), QStringLiteral("import"), {});
    describe(QStringLiteral("file.saveLayout"), QStringLiteral("另存布局"), QStringLiteral("save"), QStringLiteral("Ctrl+Shift+S"));
    describe(QStringLiteral("file.exportSettings"), QStringLiteral("导出设置"), QStringLiteral("export"), {}, false, false);
    describe(QStringLiteral("file.exportData"), QStringLiteral("导出数据"), QStringLiteral("export"), {}, false, false);
    describe(QStringLiteral("file.screenshot"), QStringLiteral("截图"), QStringLiteral("camera"), {});
    describe(QStringLiteral("layout.save"), QStringLiteral("保存布局"), QStringLiteral("save"), QStringLiteral("Ctrl+S"));
    describe(QStringLiteral("layout.import"), QStringLiteral("打开布局"), QStringLiteral("open"), QStringLiteral("Ctrl+Shift+O"));
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
        win.logs().info(QStringLiteral("导出设置：M1 接 YAML"));
    });
    win.commands().add(QStringLiteral("file.exportData"), [&win](const Ctx &) {
        win.logs().info(QStringLiteral("导出数据：M1 接 CSV/NDF"));
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
            win.logs().info(QStringLiteral("已截图：%1").arg(f));
    });

    win.commands().add(QStringLiteral("help.about"), [&win](const Ctx &) {
        DiagnosticsDialog dlg(&win, win.kernelInfo(), win.extensions().extensions(),
                              win.extensions().errors());
        dlg.exec();
    });

}


} // namespace shell
