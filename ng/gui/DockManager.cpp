#include "DockManager.h"
#include <QDockWidget>

namespace ng::gui
{



DockManager::DockManager(QMainWindow *window)
    : m_window(window) {}

Qt::DockWidgetArea DockManager::qtAreaFor(const QString &area)
{
    if (area == QStringLiteral("dock.right"))
        return Qt::RightDockWidgetArea;
    if (area == QStringLiteral("dock.bottom"))
        return Qt::BottomDockWidgetArea;
    if (area == QStringLiteral("dock.top"))
        return Qt::TopDockWidgetArea;
    return Qt::LeftDockWidgetArea; // 默认左侧
}

void DockManager::dock(const QString &area, const QString &title, QWidget *content)
{
    if (!content)
        return;

    // 同区域重复停靠：复用已有 QDockWidget，只换内容，保留用户的位置/显隐偏好。
    QDockWidget *dock = m_docks.value(area, nullptr);
    if (!dock)
    {
        dock = new QDockWidget(title, m_window);
        dock->setObjectName(QStringLiteral("ng.dock.%1").arg(area));
        dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea
                              | Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
        m_window->addDockWidget(qtAreaFor(area), dock);
        m_docks.insert(area, dock);
    }
    else
    {
        dock->setWindowTitle(title);
    }

    dock->setWidget(content);
    dock->show();
}

void DockManager::setVisible(const QString &area, bool visible)
{
    if (QDockWidget *dock = m_docks.value(area, nullptr))
        dock->setVisible(visible);
}

bool DockManager::isVisible(const QString &area) const
{
    QDockWidget *dock = m_docks.value(area, nullptr);
    return dock && dock->isVisible();
}

QWidget *DockManager::widget(const QString &area) const
{
    QDockWidget *dock = m_docks.value(area, nullptr);
    return dock ? dock->widget() : nullptr;
}

QString DockManager::title(const QString &area) const
{
    QDockWidget *dock = m_docks.value(area, nullptr);
    return dock ? dock->windowTitle() : QString();
}

} // namespace ng::gui
