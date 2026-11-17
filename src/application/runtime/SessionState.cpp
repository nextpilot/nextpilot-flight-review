#include "SessionState.h"

#include "SettingsStore.h"

#include <QFileInfo>

namespace ps::runtime
{

SessionState::SessionState(SettingsStore &store, const QString &recentFilesKey, const QString &layoutKey)
    : m_store(&store), m_recentFilesKey(recentFilesKey), m_layoutKey(layoutKey)
{
    load();
}

void SessionState::load()
{
    m_layoutPath = m_store->value(m_layoutKey).toString();
    const QStringList files = m_store->value(m_recentFilesKey).toStringList();
    for (const QString &path : files)
        if (!path.isEmpty() && QFileInfo::exists(path) && !m_recentFiles.contains(path))
            m_recentFiles.append(path);
}

void SessionState::addRecentFile(const QString &path)
{
    if (path.isEmpty())
        return;
    m_recentFiles.removeAll(path);
    m_recentFiles.prepend(path);
    while (m_recentFiles.size() > 12)
        m_recentFiles.removeLast();
    m_store->setValue(m_recentFilesKey, m_recentFiles);
}

void SessionState::clearRecentFiles()
{
    m_recentFiles.clear();
    m_store->remove(m_recentFilesKey);
}

void SessionState::setLayoutPath(const QString &path)
{
    m_layoutPath = path;
    m_store->setValue(m_layoutKey, path);
}

} // namespace ps::runtime
