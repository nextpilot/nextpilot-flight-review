#include "DataSourceSession.h"

#include "MainWindow.h"
#include "SourceModel.h"

#include <QAction>
#include <QCursor>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QSettings>

void DataSourceSession::registerDataCommands()
{
    m_mw->commands().add(QStringLiteral("app.beforeClose"), [this](const auto &) { onClose(); });
    m_mw->commands().add(QStringLiteral("data.openPath"), [this](const auto &ctx) { if (!ctx.arg.isEmpty()) openPath(ctx.arg); });
    m_mw->commands().add(QStringLiteral("data.clearCache"), [this](const auto &) { clearParseCache(); log(QStringLiteral("解析缓存已清空")); });
    m_mw->commands().add(QStringLiteral("layout.save"), [this](const auto &) { saveLayoutTo(m_mw->defaultLayoutPath()); });

    using Ctx = ps::commands::CommandContext;
    m_mw->commands().add(QStringLiteral("data.quickOpen"), [this](const Ctx &) {
        const QStringList fs = QFileDialog::getOpenFileNames(this, QStringLiteral("快速打开"), {},
            QStringLiteral("飞行日志 (*.ulg *.bin *.csv *.asc *.blf);;全部 (*)"));
        for (const QString &f : fs)
            loadFile(f);
    });
    m_mw->commands().add(QStringLiteral("data.recent"), [this](const Ctx &) {
        QMenu menu(this);
        QSettings st(QStringLiteral("NextPilot"), QStringLiteral("PlotScope"));
        const QStringList recent = st.value(QStringLiteral("recentFiles")).toStringList();
        if (recent.isEmpty())
        {
            QAction *empty = menu.addAction(QStringLiteral("暂无最近文件"));
            empty->setEnabled(false);
        }
        for (const QString &path : recent)
        {
            QAction *a = menu.addAction(QFileInfo(path).fileName());
            a->setToolTip(path);
            connect(a, &QAction::triggered, this, [this, path] { loadFile(path); });
        }
        menu.exec(QCursor::pos());
    });
    m_mw->commands().add(QStringLiteral("data.exportSnapshot"), [this](const Ctx &) { exportActiveCsv(); });
    m_mw->commands().add(QStringLiteral("data.reload"), [this](const Ctx &) {
        ps::data_source::SourceData *src = sourceById(m_activeSource);
        if (!src || src->path.isEmpty())
        {
            log(QStringLiteral("当前源没有可重载的文件"));
            return;
        }
        const QString path = src->path;
        QWidget *row = src->row;
        if (row)
            removeSourceRow(row);
        loadFile(path, false);
    });

}

