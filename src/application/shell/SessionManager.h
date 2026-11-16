#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <functional>

class QMainWindow;

namespace ps::commands
{
class CommandManager;
}

// 会话管理：窗口几何、最近文件、布局路径和未保存状态。
// 不承载数据源、绘图等业务状态。
class SessionManager
{
public:
    using DockSave = std::function<QByteArray()>;
    using DockRestore = std::function<bool(const QByteArray &)>;
    void setDockBridge(DockSave save, DockRestore restore);

    SessionManager();

    static QString defaultLayoutPath();

    void restoreWindow(QMainWindow *window);
    void restoreWindowState(QMainWindow *window);
    void saveWindow(const QMainWindow *window);

    void setLayoutPath(const QString &path);
    QString layoutPath() const { return m_layoutPath; }

    void markDirty();
    void markClean();
    bool isDirty() const { return m_dirty; }

    QStringList recentFiles() const;
    void addRecentFile(const QString &path);
    void bindCommandState(ps::commands::CommandManager &commands, std::function<void()> refreshTitle);
    void clearRecentFiles();

private:
    DockSave m_dockSave;
    DockRestore m_dockRestore;

    void load();

    QString m_layoutPath;
    QStringList m_recentFiles;
    bool m_dirty = false;
};
