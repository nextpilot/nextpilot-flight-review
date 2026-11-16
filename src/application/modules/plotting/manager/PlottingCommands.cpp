#include "PlottingSession.h"
#include "MainWindow.h"
#include <QStringList>

void PlottingSession::registerCommands()
{
    auto dirty = [this](const QString &id, const QString &text = QString()) {
        ps::commands::CommandSpec spec;
        spec.id = id;
        spec.text = text;
        spec.sessionDirty = true;
        m_mw->commands().describe(spec);
    };
    dirty(QStringLiteral("script.run"), QStringLiteral("运行脚本"));

    m_mw->commands().add(QStringLiteral("app.ready"), [this](const auto &) { refreshPropTree(currentCanvas()); });
    m_mw->commands().add(QStringLiteral("app.playhead"), [this](const auto &ctx) { applyPlayhead(ctx.arg.toDouble()); });
    m_mw->commands().add(QStringLiteral("plot.refreshTree"), [this](const auto &) { refreshPropTree(currentCanvas()); });
    m_mw->commands().add(QStringLiteral("script.run"), [this](const auto &ctx) { runScript(ctx.arg == QStringLiteral("true")); });
    m_mw->commands().add(QStringLiteral("plot.setPlayhead"), [this](const auto &ctx) { applyPlayhead(ctx.arg.toDouble()); });
    m_mw->commands().add(QStringLiteral("plot.reloadSettings"), [this](const auto &) { reloadCanvasSettings(); });

    using Ctx = ps::commands::CommandContext;
    dirty(QStringLiteral("page.new"), QStringLiteral("新建页"));
    m_mw->commands().add(QStringLiteral("page.new"), [this](const Ctx &) { newPage(); });
    dirty(QStringLiteral("page.addCanvas"), QStringLiteral("新增画布"));
    m_mw->commands().add(QStringLiteral("page.addCanvas"), [this](const Ctx &) { addCanvasToPage(); });
    dirty(QStringLiteral("page.splitHorizontal"), QStringLiteral("水平拆分"));
    m_mw->commands().add(QStringLiteral("page.splitHorizontal"), [this](const Ctx &) {
        if (auto *c = currentCanvas())
            splitCanvas(c, Qt::Horizontal);
    });
    dirty(QStringLiteral("page.splitVertical"), QStringLiteral("垂直拆分"));
    m_mw->commands().add(QStringLiteral("page.splitVertical"), [this](const Ctx &) {
        if (auto *c = currentCanvas())
            splitCanvas(c, Qt::Vertical);
    });
    dirty(QStringLiteral("page.grid"), QStringLiteral("画布网格"));
    m_mw->commands().add(QStringLiteral("page.grid"), [this](const Ctx &ctx) {
        const QStringList parts = ctx.arg.split(QLatin1Char(','));
        if (parts.size() == 2)
            applyCanvasGrid(parts.at(0).toInt(), parts.at(1).toInt());
    });
    dirty(QStringLiteral("plot.new"), QStringLiteral("新建绘图"));
    m_mw->commands().add(QStringLiteral("plot.new"), [this](const Ctx &ctx) {
        createPlotPage(ctx.arg);
    });

}
