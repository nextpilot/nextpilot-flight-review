#include "ActivityBar.h"

#include <QFile>
#include <QFontMetrics>
#include <QIcon>
#include <QLayoutItem>
#include <QPainter>
#include <QStyleOption>
#include <QToolButton>
#include <QVBoxLayout>

using namespace nextpilot::plotscope::registry;

namespace
{
QIcon activityIcon(const QString &name)
{
    if (name.isEmpty())
        return QIcon();
    const QString path = QStringLiteral(":/icons/%1.svg").arg(name);
    return QFile::exists(path) ? QIcon(path) : QIcon();
}
} // namespace

namespace nextpilot::plotscope::gui
{

ActivityButton::ActivityButton(const QString &id, const QString &label, const QString &icon,
                               QWidget *parent)
    : QToolButton(parent)
{
    setObjectName(QStringLiteral("activity.") + id);
    setToolTip(label);
    setAccessibleName(label);
    setIcon(activityIcon(icon));
    setIconSize(QSize(20, 20));
    setFixedSize(40, 40);
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    setCheckable(true);
    setAutoRaise(true);
}

void ActivityButton::setBadge(int count)
{
    if (m_badge == count)
        return;
    m_badge = qMax(0, count);
    update();
}

void ActivityButton::paintEvent(QPaintEvent *event)
{
    QToolButton::paintEvent(event);
    if (m_badge <= 0)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QString text = m_badge > 99 ? QStringLiteral("99+") : QString::number(m_badge);
    QFont font = painter.font();
    font.setPixelSize(8);
    font.setBold(true);
    painter.setFont(font);
    const int textWidth = QFontMetrics(font).horizontalAdvance(text);
    const int badgeWidth = qMax(12, textWidth + 5);
    const int badgeHeight = 12;
    const QRect rect(width() - badgeWidth - 1, 1, badgeWidth, badgeHeight);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0xE5, 0x14, 0x00));
    painter.drawRoundedRect(rect, 5, 5);
    painter.setPen(Qt::white);
    painter.drawText(rect, Qt::AlignCenter, text);
}

ActivityBar::ActivityBar(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("plotscope.activityBar"));
    setFixedWidth(48);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setStyleSheet(QStringLiteral(
        "QWidget#plotscope.activityBar{background:palette(window);"
        "border-right:1px solid palette(mid);}"
        "QToolButton{border:0;border-radius:3px;}"
        "QToolButton:hover{background:palette(alternate-base);}"
        "QToolButton:checked{background:palette(highlight);}"));

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(4, 6, 4, 6);
    m_layout->setSpacing(4);
    m_layout->addStretch(1);
}

void ActivityBar::setActivities(const QList<ActivitySpec> &activities)
{
    QHash<QString, int> badges;
    for (auto it = m_buttons.constBegin(); it != m_buttons.constEnd(); ++it)
        badges.insert(it.key(), it.value()->badge());

    while (QLayoutItem *item = m_layout->takeAt(0))
    {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    m_buttons.clear();
    m_ids.clear();
    m_settings = nullptr;

    for (const ActivitySpec &spec : activities)
    {
        auto *button = new ActivityButton(spec.id, spec.label, spec.icon, this);
        button->setBadge(badges.value(spec.id, 0));
        button->setChecked(spec.id == m_activeId);
        connect(button, &QToolButton::clicked, this, [this, id = spec.id]() {
            setActiveActivity(id);
            Q_EMIT activityActivated(id);
        });
        m_layout->addWidget(button, 0, Qt::AlignHCenter);
        m_buttons.insert(spec.id, button);
        m_ids << spec.id;
    }

    m_layout->addStretch(1);

    // 设置固定在底部，不计入 activity 顺序。
    m_settings = new QToolButton(this);
    m_settings->setObjectName(QStringLiteral("activity.settings"));
    m_settings->setToolTip(QStringLiteral("设置"));
    m_settings->setIcon(activityIcon(QStringLiteral("settings")));
    m_settings->setIconSize(QSize(18, 18));
    m_settings->setFixedSize(40, 40);
    m_settings->setAutoRaise(true);
    connect(m_settings, &QToolButton::clicked, this, &ActivityBar::settingsRequested);
    m_layout->addWidget(m_settings, 0, Qt::AlignHCenter);

    if (!m_activeId.isEmpty() && m_buttons.contains(m_activeId))
        setActiveActivity(m_activeId);
    else if (!m_ids.isEmpty())
        setActiveActivity(m_ids.first());
}

void ActivityBar::setActiveActivity(const QString &id)
{
    if (!m_buttons.contains(id))
        return;
    m_activeId = id;
    for (auto it = m_buttons.constBegin(); it != m_buttons.constEnd(); ++it)
        it.value()->setChecked(it.key() == id);
}

QStringList ActivityBar::activityIds() const
{
    return m_ids;
}

void ActivityBar::setBadge(const QString &id, int count)
{
    if (auto *button = m_buttons.value(id, nullptr))
        button->setBadge(count);
}

int ActivityBar::badge(const QString &id) const
{
    auto *button = m_buttons.value(id, nullptr);
    return button ? button->badge() : 0;
}

} // namespace nextpilot::plotscope::gui
