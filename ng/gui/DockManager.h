#pragma once

#include <QHash>
#include <QMainWindow>
#include <QString>

class QDockWidget;

// 停靠面板管理：把"插槽 -> 面板"的映射交给 QMainWindow 的 QDockWidget 机制。
//
// 职责单一：只管四边停靠的创建、归位、显隐与标题。不认识业务、不画图、
// 不管数据。面板内容由调用方（器官/工具箱）提供。
//
// 区域约定：left / right / bottom / top。
namespace ng::gui
{

class DockManager
{
public:
    explicit DockManager(QMainWindow *window);

    // 停靠一个面板到指定区域。重复停靠同一 area 会替换旧面板。
    void dock(const QString &area, const QString &title, QWidget *content);

    // 显示/隐藏某区域的面板。
    void setVisible(const QString &area, bool visible);
    bool isVisible(const QString &area) const;

    // 已停靠的面板数。
    int dockCount() const { return m_docks.size(); }

    // 取某区域的面板（可能为 nullptr）。
    QWidget *widget(const QString &area) const;

    // 面板标题（诊断用）。
    QString title(const QString &area) const;

private:
    static Qt::DockWidgetArea qtAreaFor(const QString &area);

    QMainWindow *m_window = nullptr;
    QHash<QString, QDockWidget *> m_docks;
};

} // namespace ng::gui
