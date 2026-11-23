#pragma once

#include <QHash>
#include <QStringList>
#include <QWidget>

class QVBoxLayout;
class QScrollArea;

namespace nextpilot::plotscope::gui
{

// 左栏 section 栈：一个 ADS dock 里竖排多个可折叠 section。
//
// 与旧“同侧并成 tab”不同：section 不再互斥成页签，Activity 只决定哪些
// section 可见；每个 section 的标题 24px，点标题折叠/展开。
class SideBarStack final : public QWidget
{
    Q_OBJECT
public:
    explicit SideBarStack(QWidget *parent = nullptr);

    bool addSection(const QString &id, const QString &title, QWidget *content,
                    const QString &activity);
    bool hasSection(const QString &id) const;
    QString sectionTitle(const QString &id) const;
    QWidget *sectionWidget(const QString &id) const;
    QString sectionActivity(const QString &id) const;
    QStringList sectionIds() const;
    int sectionCount() const { return m_order.size(); }

    bool setActiveActivity(const QString &activity);
    QString activeActivity() const { return m_activeActivity; }
    QString activeSectionId() const { return m_activeId; }
    QString activeSectionTitle() const;

    bool focusSection(const QString &id);
    bool setSectionVisible(const QString &id, bool visible);
    bool isSectionVisible(const QString &id) const;
    bool setSectionExpanded(const QString &id, bool expanded);
    bool isSectionExpanded(const QString &id) const;

signals:
    void sectionActivated(const QString &id);
    void sectionToggled(const QString &id, bool expanded);

private:
    struct Section
    {
        QString id;
        QString title;
        QString activity;
        QWidget *container = nullptr;
        QWidget *body = nullptr;
        QWidget *content = nullptr;
        QWidget *header = nullptr;
        bool wantedVisible = true;
        bool expanded = true;
    };

    void applyVisibility(Section &section);
    void syncHeader(Section &section) const;
    Section *find(const QString &id);

    QScrollArea *m_scroll = nullptr;
    QVBoxLayout *m_layout = nullptr;
    QHash<QString, Section> m_sections;
    QStringList m_order;
    QString m_activeActivity;
    QString m_activeId;
};

} // namespace nextpilot::plotscope::gui
