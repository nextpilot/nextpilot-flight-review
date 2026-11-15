#pragma once

#include <QString>
#include <QStringList>

class QMainWindow;

// 应用级会话：窗口几何、最近文件、布局路径和未保存状态。
// 不承载数据源、绘图等业务状态。
class AppSession
{
public:
    AppSession();

    void restoreWindow(QMainWindow *window);
    void saveWindow(const QMainWindow *window);

    void setLayoutPath(const QString &path);
    QString layoutPath() const { return m_layoutPath; }

    void markDirty();
    void markClean();
    bool isDirty() const { return m_dirty; }

    QStringList recentFiles() const;
    void addRecentFile(const QString &path);
    void clearRecentFiles();

private:
    void load();

    QString m_layoutPath;
    QStringList m_recentFiles;
    bool m_dirty = false;
};
