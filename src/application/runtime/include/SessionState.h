#pragma once

#include <QString>
#include <QStringList>

namespace ps::runtime
{

class SettingsStore;

// 进程级会话状态：最近文件、上次布局路径。
// 只做键值读写和最近文件裁剪，不依赖 Widgets；窗口几何仍由 shell 管理。
class SessionState
{
public:
    SessionState(SettingsStore &store, const QString &recentFilesKey, const QString &layoutKey);

    QStringList recentFiles() const { return m_recentFiles; }
    void addRecentFile(const QString &path);
    void clearRecentFiles();

    QString layoutPath() const { return m_layoutPath; }
    void setLayoutPath(const QString &path);

    void reload() { load(); }

private:
    void load();

    SettingsStore *m_store = nullptr;
    QString m_recentFilesKey;
    QString m_layoutKey;
    QStringList m_recentFiles;
    QString m_layoutPath;
};

} // namespace ps::runtime
