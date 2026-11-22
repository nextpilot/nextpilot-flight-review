#include "CellTitleBar.h"

#include "DockAreaTitleBar.h"
#include "DockAreaWidget.h"
#include "DockWidget.h"
#include "fastplot/Axes.h"

#include <QColor>
#include <QCoreApplication>
#include <QFile>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QStyle>
#include <QToolButton>

namespace nextpilot::plotscope::gui
{

namespace
{

// 按钮图标：资源缺失时返回空图标，按钮退化为文字。
QIcon barIcon(const char *name)
{
    const QString path = QStringLiteral(":/icons/%1.svg").arg(QLatin1String(name));
    return QFile::exists(path) ? QIcon(path) : QIcon();
}

} // namespace

CellTitleBar::CellTitleBar(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("cellHoverBar"));
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_Hover, true);

    QColor barBg = parent ? parent->palette().alternateBase().color() : palette().alternateBase().color();
    barBg.setAlpha(255);
    setFixedHeight(fastplot::Axes::kTitleBarHeight - 2);
    setStyleSheet(QStringLiteral(
        "#cellHoverBar { background: %1; }"
        "#cellHoverBar[dimmed=\"true\"] { background: transparent; }"
        "#cellHoverBar[dimmed=\"true\"] QLabel { color: transparent; }"
        " QToolButton { border: none; padding: 0px; border-radius: 2px; background: transparent; }"
        " QToolButton:hover { background: rgba(127,127,127,0.28); }"
        " QLabel { background: transparent; color: palette(text); }")
                      .arg(barBg.name(QColor::HexArgb)));

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(6, 0, 2, 0);
    lay->setSpacing(0);

    m_title = new QLabel(QStringLiteral("图"), this);
    m_title->setToolTip(QStringLiteral("拖动这里可重新排列；双击改名"));
    m_title->setCursor(Qt::OpenHandCursor);
    m_title->installEventFilter(this);
    installEventFilter(this);

    auto makeButton = [this](const char *iconName, const QString &fallbackText, const QString &tip) {
        auto *btn = new QToolButton(this);
        btn->setAutoRaise(true);
        btn->setToolTip(tip);
        btn->setIconSize(QSize(14, 14));
        btn->setFixedSize(20, fastplot::Axes::kTitleBarHeight - 4);
        btn->setCursor(Qt::PointingHandCursor);
        const QIcon icon = barIcon(iconName);
        if (icon.isNull())
            btn->setText(fallbackText);
        else
            btn->setIcon(icon);
        m_buttons.append(btn);
        return btn;
    };

    // 顺序：左右拆分 · 上下拆分 · 最大化 · 书签 · A/B · 关闭。
    QToolButton *splitH = makeButton("split_h", QStringLiteral("◫"), QStringLiteral("左右拆分"));
    QToolButton *splitV = makeButton("split_v", QStringLiteral("◱"), QStringLiteral("上下拆分"));
    QToolButton *maximize = makeButton("maximize", QStringLiteral("⤢"), QStringLiteral("最大化 / 还原"));
    QToolButton *bookmark = makeButton("bookmark", QStringLiteral("◇"), QStringLiteral("在当前时刻加书签"));
    QToolButton *ab = makeButton("ab", QStringLiteral("AB"), QStringLiteral("A/B 区间测量"));
    QToolButton *close = makeButton("close", QStringLiteral("✕"), QStringLiteral("关闭这一格"));

    QObject::connect(splitH, &QToolButton::clicked, this, [this]() { Q_EMIT splitClicked(true); });
    QObject::connect(splitV, &QToolButton::clicked, this, [this]() { Q_EMIT splitClicked(false); });
    QObject::connect(maximize, &QToolButton::clicked, this, [this]() { Q_EMIT maximizeClicked(); });
    QObject::connect(bookmark, &QToolButton::clicked, this, [this]() { Q_EMIT bookmarkClicked(); });
    QObject::connect(ab, &QToolButton::clicked, this, [this]() { Q_EMIT abClicked(); });
    QObject::connect(close, &QToolButton::clicked, this, [this]() { Q_EMIT closeClicked(); });

    lay->addWidget(m_title, 1);
    lay->addWidget(splitH);
    lay->addWidget(splitV);
    lay->addWidget(maximize);
    lay->addWidget(bookmark);
    lay->addWidget(ab);
    lay->addWidget(close);
}

void CellTitleBar::setDockWidget(ads::CDockWidget *dockWidget)
{
    m_dockWidget = dockWidget;
}

void CellTitleBar::setTitle(const QString &title)
{
    if (m_title)
        m_title->setText(title);
}

QString CellTitleBar::title() const
{
    return m_title ? m_title->text() : QString();
}

void CellTitleBar::setShown(bool shown)
{
    // 页签模式下格名与按钮由页签条承担，本条保持收起。
    if (m_suppressed && shown)
        return;
    if (m_shown == shown)
        return;
    m_shown = shown;
    setProperty("dimmed", !shown);
    for (QWidget *btn : m_buttons)
        btn->setVisible(shown);
    // 动态属性变了要重新抛光，样式表里的 dimmed 规则才生效。
    style()->unpolish(this);
    style()->polish(this);
    update();
}

void CellTitleBar::setSuppressed(bool suppressed)
{
    if (m_suppressed == suppressed)
        return;
    m_suppressed = suppressed;
    if (suppressed)
        setShown(false);
}

bool CellTitleBar::eventFilter(QObject *watched, QEvent *event)
{
    const bool onBar = (watched == this || watched == m_title);
    if (!onBar)
        return QWidget::eventFilter(watched, event);

    switch (event->type())
    {
    case QEvent::MouseButtonPress:
        Q_EMIT activated();
        // 起点给 (0,0)：浮动预览贴光标，不按点在条上的偏移。
        forwardToTitleBar(event, true);
        return true;
    case QEvent::MouseMove:
        // 原始事件直转：ADS 按位移判断是否起拖。
        forwardToTitleBar(event, false);
        return true;
    case QEvent::MouseButtonRelease:
        forwardToTitleBar(event, false);
        return true;
    case QEvent::MouseButtonDblClick:
        if (watched == m_title)
        {
            rename();
            return true;
        }
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

void CellTitleBar::forwardToTitleBar(QEvent *event, bool syntheticPress)
{
    if (!m_dockWidget)
        return;
    ads::CDockAreaWidget *area = m_dockWidget->dockAreaWidget();
    if (!area)
        return;
    ads::CDockAreaTitleBar *titleBar = area->titleBar();
    if (!titleBar)
        return;

    if (syntheticPress)
    {
        auto *mouse = static_cast<QMouseEvent *>(event);
        QMouseEvent forwarded(QEvent::MouseButtonPress, QPointF(0, 0), mouse->globalPosition(),
                              mouse->button(), mouse->buttons(), mouse->modifiers());
        QCoreApplication::sendEvent(titleBar, &forwarded);
        return;
    }
    QCoreApplication::sendEvent(titleBar, event);
}

void CellTitleBar::rename()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("重命名画布"), QStringLiteral("名称"),
                                               QLineEdit::Normal, title(), &ok);
    const QString trimmed = name.trimmed();
    if (!ok || trimmed.isEmpty())
        return;
    setTitle(trimmed);
    if (m_dockWidget)
        m_dockWidget->setWindowTitle(trimmed);
    Q_EMIT titleEdited(trimmed);
}

} // namespace nextpilot::plotscope::gui
