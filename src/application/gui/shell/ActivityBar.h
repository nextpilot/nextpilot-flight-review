#pragma once

#include "Contribution.h"

#include <QHash>
#include <QToolButton>
#include <QWidget>

class QVBoxLayout;

namespace nextpilot::plotscope::gui
{

// Activity Bar 的单个入口：图标 + 选中态 + 右上角 badge。
class ActivityButton final : public QToolButton
{
    Q_OBJECT
public:
    explicit ActivityButton(const QString &id, const QString &label, const QString &icon,
                            QWidget *parent = nullptr);

    void setBadge(int count);
    int badge() const { return m_badge; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int m_badge = 0;
};

// VS Code 式 Activity Bar：48px 竖条，固定在外壳左侧、全高（不含状态栏）。
// 只负责“点了哪个 activity”，切换侧栏/ribbon 由 ShellBinder 编排。
class ActivityBar final : public QWidget
{
    Q_OBJECT
public:
    explicit ActivityBar(QWidget *parent = nullptr);

    void setActivities(const QList<nextpilot::plotscope::registry::ActivitySpec> &activities);
    void setActiveActivity(const QString &id);
    QString activeActivity() const { return m_activeId; }
    QStringList activityIds() const;

    void setBadge(const QString &id, int count);
    int badge(const QString &id) const;

signals:
    void activityActivated(const QString &id);
    void settingsRequested();

private:
    void paintEvent(QPaintEvent *event) override;

    QVBoxLayout *m_layout = nullptr;
    QHash<QString, ActivityButton *> m_buttons;
    QStringList m_ids;
    QString m_activeId;
    QToolButton *m_settings = nullptr;
};

} // namespace nextpilot::plotscope::gui
