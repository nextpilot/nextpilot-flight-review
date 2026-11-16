#include "SessionManager.h"
#include "CommandManager.h"
#include "SettingsManager.h"
#include "ShellPaths.h"

#include <QFileInfo>
#include <QMainWindow>

SessionManager::SessionManager()
{
    load();
}

QString SessionManager::defaultLayoutPath()
{
    return ShellPaths::defaultLayoutPath();
}

void SessionManager::load()
{
    m_layoutPath = SettingsManager::instance().value(QStringLiteral("lastLayout")).toString();
    const QStringList files = SettingsManager::instance().value(QStringLiteral("recentFiles")).toStringList();
    for (const QString &path : files)
    {
        if (!path.isEmpty() && QFileInfo::exists(path) && !m_recentFiles.contains(path))
            m_recentFiles.append(path);
    }
}

void SessionManager::restoreWindow(QMainWindow *window)
{
    if (!window)
        return;
    const QByteArray geometry = SettingsManager::instance().value(QStringLiteral("windowGeometry")).toByteArray();
    if (!geometry.isEmpty())
        window->restoreGeometry(geometry);
}

void SessionManager::restoreWindowState(QMainWindow *window)
{
    if (!window)
        return;
    const QByteArray state = SettingsManager::instance().value(QStringLiteral("windowState")).toByteArray();
    if (!state.isEmpty())
        window->restoreState(state);
}

void SessionManager::saveWindow(const QMainWindow *window)
{
    if (!window)
        return;
    SettingsManager::instance().setValue(QStringLiteral("windowGeometry"), window->saveGeometry());
    SettingsManager::instance().setValue(QStringLiteral("windowState"), window->saveState());
    SettingsManager::instance().sync();
}

void SessionManager::setLayoutPath(const QString &path)
{
    m_layoutPath = path;
    SettingsManager::instance().setValue(QStringLiteral("lastLayout"), path);
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
    return m_recentFiles;
}

void SessionManager::addRecentFile(const QString &path)
{
    if (path.isEmpty())
        return;
    m_recentFiles.removeAll(path);
    m_recentFiles.prepend(path);
    while (m_recentFiles.size() > 12)
        m_recentFiles.removeLast();
    SettingsManager::instance().setValue(QStringLiteral("recentFiles"), m_recentFiles);
}

void SessionManager::clearRecentFiles()
{
    m_recentFiles.clear();
    SettingsManager::instance().remove(QStringLiteral("recentFiles"));
}

void SessionManager::bindCommandState(ps::commands::CommandManager &commands, std::function<void()> refreshTitle)
{
    commands.setInvokeHook([this, &commands, refreshTitle = std::move(refreshTitle)](const QString &id) {
        if (id == QLatin1String("layout.save") || id == QLatin1String("layout.import"))
        {
            markClean();
            if (refreshTitle)
                refreshTitle();
            return;
        }
        const auto *spec = commands.spec(id);
        if (spec && spec->sessionDirty)
        {
            markDirty();
            if (refreshTitle)
                refreshTitle();
        }
    });
}
