#include "SideBarStack.h"

#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QVBoxLayout>

#include <functional>

namespace nextpilot::plotscope::gui
{

// 左对齐的 section 标题：QToolButton/QPushButton 的 QSS text-align 在
// 折叠后会让标题居中，这里自绘保证标题始终贴左。
class SectionHeader final : public QWidget
{
public:
    explicit SectionHeader(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedHeight(24);
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setState(bool expanded, const QString &title)
    {
        m_expanded = expanded;
        m_title = title;
        update();
    }

    std::function<void()> onClicked;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().alternateBase());

        QFont textFont = font();
        textFont.setPixelSize(11);
        textFont.setBold(true);
        painter.setFont(textFont);
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(rect().adjusted(6, 0, -6, 0), Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("%1 %2").arg(m_expanded ? QStringLiteral("▾")
                                                                  : QStringLiteral("▸"),
                                                      m_title));

        painter.setPen(palette().color(QPalette::Mid));
        painter.drawLine(rect().bottomLeft(), rect().bottomRight());
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()))
        {
            if (onClicked)
                onClicked();
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

private:
    bool m_expanded = true;
    QString m_title;
};

SideBarStack::SideBarStack(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(m_scroll, 1);

    auto *inner = new QWidget(m_scroll);
    m_layout = new QVBoxLayout(inner);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(1);
    m_layout->addStretch(1);
    m_scroll->setWidget(inner);
}

bool SideBarStack::addSection(const QString &id, const QString &title, QWidget *content,
                              const QString &activity)
{
    if (id.isEmpty() || !content || m_sections.contains(id))
        return false;

    Section section;
    section.id = id;
    section.title = title;
    section.activity = activity;
    section.container = new QWidget(m_scroll);
    section.container->setObjectName(QStringLiteral("sidebar.section.%1").arg(id));

    auto *layout = new QVBoxLayout(section.container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new SectionHeader(section.container);
    section.header = header;
    section.header->setObjectName(QStringLiteral("sidebar.header.%1").arg(id));
    layout->addWidget(section.header);

    section.body = new QWidget(section.container);
    section.content = content;
    auto *bodyLayout = new QVBoxLayout(section.body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    bodyLayout->addWidget(content);
    layout->addWidget(section.body, 1);

    m_sections.insert(id, section);
    m_order << id;
    m_layout->insertWidget(m_layout->count() - 1, section.container, 1);

    header->onClicked = [this, id, header]() {
        const bool expanded = !m_sections.value(id).expanded;
        setSectionExpanded(id, expanded);
        m_activeId = id;
        Q_EMIT sectionToggled(id, expanded);
        Q_EMIT sectionActivated(id);
    };

    if (m_activeId.isEmpty())
        m_activeId = id;
    if (!m_activeActivity.isEmpty() && section.activity != m_activeActivity)
        section.wantedVisible = false;
    applyVisibility(m_sections[id]);
    syncHeader(m_sections[id]);
    return true;
}

bool SideBarStack::hasSection(const QString &id) const
{
    return m_sections.contains(id);
}

QString SideBarStack::sectionTitle(const QString &id) const
{
    auto it = m_sections.constFind(id);
    return it == m_sections.constEnd() ? QString() : it->title;
}

QWidget *SideBarStack::sectionWidget(const QString &id) const
{
    auto it = m_sections.constFind(id);
    return it == m_sections.constEnd() ? nullptr : it->content;
}

QString SideBarStack::sectionActivity(const QString &id) const
{
    auto it = m_sections.constFind(id);
    return it == m_sections.constEnd() ? QString() : it->activity;
}

QStringList SideBarStack::sectionIds() const
{
    return m_order;
}

bool SideBarStack::setActiveActivity(const QString &activity)
{
    bool any = false;
    for (const QString &id : m_order)
        if (m_sections.value(id).activity == activity)
            any = true;
    if (!any)
        return false; // 该 activity 没有左栏 section：保留当前侧栏内容

    m_activeActivity = activity;
    bool activeStillVisible = false;
    for (const QString &id : m_order)
    {
        Section &section = m_sections[id];
        section.wantedVisible = (section.activity == activity);
        applyVisibility(section);
        if (section.wantedVisible && section.id == m_activeId)
            activeStillVisible = true;
    }
    if (!activeStillVisible)
    {
        for (const QString &id : m_order)
        {
            if (m_sections.value(id).wantedVisible)
            {
                m_activeId = id;
                break;
            }
        }
    }
    return true;
}

QString SideBarStack::activeSectionTitle() const
{
    if (!m_activeId.isEmpty() && isSectionVisible(m_activeId))
        return sectionTitle(m_activeId);
    for (const QString &id : m_order)
        if (isSectionVisible(id))
            return sectionTitle(id);
    return QString();
}

bool SideBarStack::focusSection(const QString &id)
{
    Section *section = find(id);
    if (!section)
        return false;
    if (section->activity != m_activeActivity)
        setActiveActivity(section->activity);
    section->wantedVisible = true;
    applyVisibility(*section);
    setSectionExpanded(id, true);
    m_activeId = id;
    Q_EMIT sectionActivated(id);
    return true;
}

bool SideBarStack::setSectionVisible(const QString &id, bool visible)
{
    Section *section = find(id);
    if (!section)
        return false;
    section->wantedVisible = visible;
    applyVisibility(*section);
    return true;
}

bool SideBarStack::isSectionVisible(const QString &id) const
{
    auto it = m_sections.constFind(id);
    return it != m_sections.constEnd() && it->wantedVisible && it->container->isVisibleTo(this);
}

bool SideBarStack::setSectionExpanded(const QString &id, bool expanded)
{
    Section *section = find(id);
    if (!section)
        return false;
    section->expanded = expanded;
    section->body->setVisible(expanded && section->wantedVisible);
    syncHeader(*section);
    return true;
}

bool SideBarStack::isSectionExpanded(const QString &id) const
{
    auto it = m_sections.constFind(id);
    return it != m_sections.constEnd() && it->expanded;
}

void SideBarStack::applyVisibility(Section &section)
{
    section.container->setVisible(section.wantedVisible);
    section.body->setVisible(section.expanded && section.wantedVisible);
}

void SideBarStack::syncHeader(Section &section) const
{
    static_cast<SectionHeader *>(section.header)->setState(section.expanded, section.title);
}

SideBarStack::Section *SideBarStack::find(const QString &id)
{
    auto it = m_sections.find(id);
    return it == m_sections.end() ? nullptr : &it.value();
}

} // namespace nextpilot::plotscope::gui
