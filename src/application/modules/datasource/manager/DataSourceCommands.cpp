#include "DataSourceSession.h"
#include "SessionManager.h"

#include "datasource/DataSourceRegistry.h"

#include "MainWindow.h"
#include "SourceModel.h"

#include <QAction>
#include <QCursor>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>

void DataSourceSession::registerDataCommands()
{
    auto dirty = [this](const QString &id, const QString &text = QString()) {
        ps::commands::CommandSpec spec;
        spec.id = id;
        spec.text = text;
        spec.sessionDirty = true;
        m_mw->commands().describe(spec);
    };
    dirty(QStringLiteral("data.openPath"), QStringLiteral("打开数据"));
    dirty(QStringLiteral("data.quickOpen"), QStringLiteral("快速打开"));
    dirty(QStringLiteral("data.recent"), QStringLiteral("最近文件"));
    dirty(QStringLiteral("data.reload"), QStringLiteral("重新加载"));

    m_mw->commands().add(QStringLiteral("app.beforeClose"), [this](const auto &) { onClose(); });
    m_mw->commands().add(QStringLiteral("file.open"), [this](const auto &) {
        QStringList filters;
        for (const DataSourceEntry &entry : DataSourceRegistry::instance().fileEntries())
            if (!entry.filter.isEmpty())
                filters.append(entry.filter);
        filters.append(QStringLiteral("全部 (*)"));
        const QStringList fs = QFileDialog::getOpenFileNames(m_mw, QStringLiteral("打开文件"), {},
                                                             filters.join(QStringLiteral(";;")));
        for (const QString &f : fs)
            loadFile(f);
    });
    m_mw->commands().add(QStringLiteral("file.exportData"), [this](const auto &) { exportActiveCsv(); });
    m_mw->commands().add(QStringLiteral("data.openPath"), [this](const auto &ctx) { if (!ctx.arg.isEmpty()) openPath(ctx.arg); });
    m_mw->commands().add(QStringLiteral("data.clearCache"), [this](const auto &) { clearParseCache(); log(QStringLiteral("解析缓存已清空")); });
    m_mw->commands().add(QStringLiteral("layout.save"), [this](const auto &ctx) {
        const QString path = ctx.arg.isEmpty() ? m_mw->session().layoutPath() : ctx.arg;
        saveLayoutTo(path.isEmpty() ? SessionManager::defaultLayoutPath() : path);
    });
    m_mw->commands().add(QStringLiteral("layout.import"), [this](const auto &ctx) {
        if (!ctx.arg.isEmpty())
            importLayoutFrom(ctx.arg);
    });

    using Ctx = ps::commands::CommandContext;
    m_mw->commands().add(QStringLiteral("data.quickOpen"), [this](const Ctx &) {
        QStringList filters;
        for (const DataSourceEntry &entry : DataSourceRegistry::instance().fileEntries())
            if (!entry.filter.isEmpty())
                filters.append(entry.filter);
        filters.append(QStringLiteral("全部 (*)"));
        const QStringList fs = QFileDialog::getOpenFileNames(this, QStringLiteral("快速打开"), {},
                                                             filters.join(QStringLiteral(";;")));
        for (const QString &f : fs)
            loadFile(f);
    });
    m_mw->commands().add(QStringLiteral("data.recent"), [this](const Ctx &) {
        QMenu menu(this);
        const QStringList recent = m_mw->session().recentFiles();
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

