#include "SessionManager.h"
#include "SettingsManager.h"
#include "ShellPaths.h"

#include <QMainWindow>

SessionManager::SessionManager() = default;

QString SessionManager::defaultLayoutPath()
{
    return ShellPaths::defaultLayoutPath();
}

void SessionManager::restoreWindow(QMainWindow *window)
{
    if (!window)
        return;
    const QByteArray geometry = SettingsManager::instance().get(ps::settings::kWindowGeometry);
    if (!geometry.isEmpty())
        window->restoreGeometry(geometry);
}

void SessionManager::restoreWindowState(QMainWindow *window)
{
    if (!window)
        return;
    const QByteArray state = SettingsManager::instance().get(ps::settings::kWindowState);
    if (!state.isEmpty())
    {
        if (!m_dockRestore || !m_dockRestore(state))
            window->restoreState(state);
    }
}

void SessionManager::saveWindow(const QMainWindow *window)
{
    if (!window)
        return;
    SettingsManager::instance().set(ps::settings::kWindowGeometry, window->saveGeometry());
    SettingsManager::instance().set(ps::settings::kWindowState,
                                    m_dockSave ? m_dockSave() : window->saveState());
    SettingsManager::instance().sync();
}

void SessionManager::setDockBridge(DockSave save, DockRestore restore)
{
    m_dockSave = std::move(save);
    m_dockRestore = std::move(restore);
}

void SessionManager::setLayoutPath(const QString &path)
{
    SettingsManager::instance().sessionState().setLayoutPath(path);
}

QString SessionManager::layoutPath() const
{
    return SettingsManager::instance().sessionState().layoutPath();
}

void SessionManager::markDirty()
{
    m_dirty = true;
}

void SessionManager::markClean()
{
    m_dirty = false;
}

QStringList SessionManager::recentFiles() const
{
    return SettingsManager::instance().sessionState().recentFiles();
}

void SessionManager::addRecentFile(const QString &path)
{
    SettingsManager::instance().sessionState().addRecentFile(path);
}

void SessionManager::clearRecentFiles()
{
    SettingsManager::instance().sessionState().clearRecentFiles();
}
