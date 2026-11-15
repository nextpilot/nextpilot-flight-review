#include "PlottingSession.h"
#include "MainWindow.h"
#include <QStringList>

void PlottingSession::registerCommands()
{
    m_mw->commands().add(QStringLiteral("plot.refreshTree"), [this](const auto &) { refreshPropTree(currentCanvas()); });
    m_mw->commands().add(QStringLiteral("script.run"), [this](const auto &ctx) { runScript(ctx.arg == QStringLiteral("true")); });
    m_mw->commands().add(QStringLiteral("plot.setPlayhead"), [this](const auto &ctx) { applyPlayhead(ctx.arg.toDouble()); });
    m_mw->commands().add(QStringLiteral("plot.reloadSettings"), [this](const auto &) { reloadCanvasSettings(); });

    using Ctx = ps::commands::CommandContext;
    m_mw->commands().add(QStringLiteral("page.new"), [this](const Ctx &) { newPage(); });
    m_mw->commands().add(QStringLiteral("page.addCanvas"), [this](const Ctx &) { addCanvasToPage(); });
    m_mw->commands().add(QStringLiteral("page.splitHorizontal"), [this](const Ctx &) {
        if (auto *c = currentCanvas())
            splitCanvas(c, Qt::Horizontal);
    });
    m_mw->commands().add(QStringLiteral("page.splitVertical"), [this](const Ctx &) {
        if (auto *c = currentCanvas())
            splitCanvas(c, Qt::Vertical);
    });

    m_mw->commands().add(QStringLiteral("page.grid"), [this](const Ctx &ctx) {
        const QStringList parts = ctx.arg.split(QLatin1Char(','));
        if (parts.size() == 2)
            applyCanvasGrid(parts.at(0).toInt(), parts.at(1).toInt());
    });

    m_mw->commands().add(QStringLiteral("plot.new"), [this](const Ctx &ctx) {
        createPlotPage(ctx.arg);
    });

}
