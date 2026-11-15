#include "AppSession.h"

#include <QFileInfo>
#include <QMainWindow>
#include <QSettings>

AppSession::AppSession()
{
    load();
}

void AppSession::load()
{
    QSettings st(QStringLiteral("NextPilot"), QStringLiteral("PlotScope"));
    m_layoutPath = st.value(QStringLiteral("lastLayout")).toString();
    const QStringList files = st.value(QStringLiteral("recentFiles")).toStringList();
    for (const QString &path : files)
    {
        if (!path.isEmpty() && QFileInfo::exists(path) && !m_recentFiles.contains(path))
            m_recentFiles.append(path);
    }
}

void AppSession::restoreWindow(QMainWindow *window)
{
    if (!window)
        return;
    QSettings st(QStringLiteral("NextPilot"), QStringLiteral("PlotScope"));
    const QByteArray geometry = st.value(QStringLiteral("windowGeometry")).toByteArray();
    if (!geometry.isEmpty())
        window->restoreGeometry(geometry);
    const QByteArray state = st.value(QStringLiteral("windowState")).toByteArray();
    if (!state.isEmpty())
        window->restoreState(state);
}

void AppSession::saveWindow(const QMainWindow *window)
{
    if (!window)
        return;
    QSettings st(QStringLiteral("NextPilot"), QStringLiteral("PlotScope"));
    st.setValue(QStringLiteral("windowGeometry"), window->saveGeometry());
    st.setValue(QStringLiteral("windowState"), window->saveState());
}

void AppSession::setLayoutPath(const QString &path)
{
    m_layoutPath = path;
    QSettings st(QStringLiteral("NextPilot"), QStringLiteral("PlotScope"));
    st.setValue(QStringLiteral("lastLayout"), path);
}

void AppSession::markDirty()
{
    m_dirty = true;
}

void AppSession::markClean()
{
    m_dirty = false;
}

QStringList AppSession::recentFiles() const
{
    return m_recentFiles;
}

void AppSession::addRecentFile(const QString &path)
{
    if (path.isEmpty())
        return;
    m_recentFiles.removeAll(path);
    m_recentFiles.prepend(path);
    while (m_recentFiles.size() > 12)
        m_recentFiles.removeLast();
    QSettings st(QStringLiteral("NextPilot"), QStringLiteral("PlotScope"));
    st.setValue(QStringLiteral("recentFiles"), m_recentFiles);
}

void AppSession::clearRecentFiles()
{
    m_recentFiles.clear();
    QSettings st(QStringLiteral("NextPilot"), QStringLiteral("PlotScope"));
    st.remove(QStringLiteral("recentFiles"));
}
