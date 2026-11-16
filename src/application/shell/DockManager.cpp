#include "DockManager.h"
#include "CommandManager.h"

#include "Component.h"

#include <QAction>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

#include <algorithm>
#include <functional>

namespace
{

void dockToArea(QDockWidget *dock, QMainWindow *win, Qt::DockWidgetArea area);

class DockTitleBar : public QWidget
{
public:
    explicit DockTitleBar(QMainWindow *win, QDockWidget *dock)
        : QWidget(dock), m_win(win), m_dock(dock)
    {
        setFixedHeight(32);
        setCursor(Qt::SizeAllCursor);
        setToolTip(QStringLiteral("拖动到窗口边缘可停靠，双击切换浮动"));
    }
    QSize sizeHint() const override { return QSize(QWidget::sizeHint().width(), 32); }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton)
        {
            QWidget::mousePressEvent(e);
            return;
        }
        m_pressPos = e->globalPosition().toPoint();
        m_dragging = false;
        e->accept();
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (!(e->buttons() & Qt::LeftButton) || !m_dock)
        {
            QWidget::mouseMoveEvent(e);
            return;
        }
        const QPoint gp = e->globalPosition().toPoint();
        if (!m_dragging && (gp - m_pressPos).manhattanLength() < 6)
            return;
        if (!m_dragging)
        {
            m_dragging = true;
            m_offset = gp - m_dock->frameGeometry().topLeft();
            if (!m_dock->isFloating())
                m_dock->setFloating(true);
        }
        m_dock->move(gp - m_offset);
        e->accept();
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton || !m_dragging)
        {
            QWidget::mouseReleaseEvent(e);
            return;
        }
        m_dragging = false;
        if (!m_dock || !m_win)
            return;
        const QPoint gp = e->globalPosition().toPoint();
        const QRect winRect = m_win->frameGeometry();
        if (!winRect.adjusted(-24, -24, 24, 24).contains(gp))
            return;
        const int dl = qAbs(gp.x() - winRect.left());
        const int dr = qAbs(gp.x() - winRect.right());
        const int db = qAbs(gp.y() - winRect.bottom());
        const int dt = qAbs(gp.y() - winRect.top());
        const int minD = std::min(std::min(dl, dr), std::min(db, dt));
        if (minD > 150)
            return;
        Qt::DockWidgetArea area = dr <= dl ? Qt::RightDockWidgetArea : Qt::LeftDockWidgetArea;
        if (db < std::min(dl, dr) && db <= dt)
            area = Qt::BottomDockWidgetArea;
        else if (dt < std::min(dl, dr) && dt < db)
            area = Qt::TopDockWidgetArea;
        dockToArea(m_dock, m_win, area);
        e->accept();
    }

    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && m_dock)
        {
            m_dock->setFloating(!m_dock->isFloating());
            e->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(e);
    }

private:
    QMainWindow *m_win = nullptr;
    QDockWidget *m_dock = nullptr;
    QPoint m_pressPos;
    QPoint m_offset;
    bool m_dragging = false;
};

class DockAutoHideTab : public QWidget
{
public:
    using Handler = std::function<void()>;

    DockAutoHideTab(QWidget *parent, const QString &title, Qt::DockWidgetArea area, Handler handler)
        : QWidget(parent), m_title(title), m_handler(std::move(handler))
    {
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
        setToolTip(QStringLiteral("展开 %1").arg(title));
        setArea(area);
    }

    void setArea(Qt::DockWidgetArea area)
    {
        m_area = area;
        const int h = std::clamp(48 + int(m_title.size()) * 13, 96, 200);
        if (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea)
            setFixedSize(26, h);
        else
            setFixedSize(h, 26);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        p.setBrush(m_hover ? QColor("#1f2a33") : QColor("#161b20"));
        p.setPen(QPen(m_hover ? QColor("#f2b134") : QColor("#2d3841"), 1));
        p.drawRoundedRect(r, 2, 2);

        p.setPen(QColor("#f2b134"));
        QFont arrow = font();
        arrow.setPixelSize(12);
        p.setFont(arrow);
        const QString mark = (m_area == Qt::LeftDockWidgetArea) ? QStringLiteral("›")
                            : (m_area == Qt::RightDockWidgetArea) ? QStringLiteral("‹")
                            : (m_area == Qt::TopDockWidgetArea) ? QStringLiteral("▼") : QStringLiteral("▲");
        if (m_area == Qt::LeftDockWidgetArea || m_area == Qt::RightDockWidgetArea)
            p.drawText(QRectF(0, 2, width(), 16), Qt::AlignCenter, mark);
        else
            p.drawText(QRectF(2, 0, 16, height()), Qt::AlignCenter, mark);

        QFont f = font();
        f.setPixelSize(11);
        p.setFont(f);
        p.setPen(QColor("#c9d1d9"));
        p.save();
        if (m_area == Qt::LeftDockWidgetArea || m_area == Qt::RightDockWidgetArea)
        {
            p.translate(width() / 2.0, height() / 2.0);
            p.rotate(m_area == Qt::LeftDockWidgetArea ? 90 : -90);
            p.drawText(QRectF(-height() / 2.0 + 10, -width() / 2.0, height() - 16, width()),
                       Qt::AlignCenter, m_title);
        }
        else
        {
            p.drawText(QRectF(18, 0, width() - 22, height()), Qt::AlignCenter, m_title);
        }
        p.restore();
    }

    void enterEvent(QEnterEvent *) override { m_hover = true; update(); }
    void leaveEvent(QEvent *) override { m_hover = false; update(); }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && m_handler)
            m_handler();
    }

private:
    QString m_title;
    Handler m_handler;
    Qt::DockWidgetArea m_area = Qt::RightDockWidgetArea;
    bool m_hover = false;
};

QHash<QDockWidget *, QWidget *> g_handles;

void dockToArea(QDockWidget *dock, QMainWindow *win, Qt::DockWidgetArea area)
{
    if (!dock || !win)
        return;
    if (dock->isFloating())
        dock->setFloating(false);
    win->removeDockWidget(dock);
    win->addDockWidget(area, dock);
    dock->show();
    dock->raise();
}

void positionHandle(QDockWidget *dock, QWidget *btn, QMainWindow *win, int index, int total)
{
    if (!dock || !btn || !win || !win->centralWidget())
        return;
    const QRect cg = win->centralWidget()->geometry();
    const Qt::DockWidgetArea area = win->dockWidgetArea(dock);
    const int W = 26, GAP = 6;
    const int H = qMax(96, btn->height());
    if (auto *tab = dynamic_cast<DockAutoHideTab *>(btn))
        tab->setArea(area);
    if (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea)
    {
        const int x = area == Qt::LeftDockWidgetArea ? cg.left() + 2 : cg.right() - W - 2;
        const int block = total * H + (total - 1) * GAP;
        const int y = cg.center().y() - block / 2 + index * (H + GAP);
        btn->move(x, y);
    }
    else
    {
        const int y = area == Qt::TopDockWidgetArea ? cg.top() + 2 : cg.bottom() - W - 2;
        const int block = total * H + (total - 1) * GAP;
        const int x = cg.center().x() - block / 2 + index * (H + GAP);
        btn->move(x, y);
    }
    btn->raise();
}

QString dockIcon(const QDockWidget *dock)
{
    const QString icon = dock->property("psIcon").toString();
    return icon.isEmpty() ? QStringLiteral(":/icons/panel.svg") : icon;
}

void showDockMenu(QToolButton *button, QMenu *menu)
{
    const QPoint below = button->mapToGlobal(QPoint(button->width(), button->height()));
    const QPoint topLeft = button->mapToGlobal(QPoint(0, 0));
    const int w = menu->sizeHint().width();
    const int h = menu->sizeHint().height();
    const QRect avail = button->screen() ? button->screen()->availableGeometry() : QRect(0, 0, 1920, 1080);
    QPoint pos(below.x() - w, below.y());
    pos.setX(qMax(avail.left() + 4, qMin(pos.x(), avail.right() - w - 4)));
    if (pos.y() + h > avail.bottom())
        pos.setY(topLeft.y() - h);
    menu->exec(pos);
}

} // namespace

DockManager::DockManager(QMainWindow *window)
    : m_window(window)
{
}

void DockManager::createFromRegistry(const ps::components::Registry &registry)
{
    auto docks = registry.docks();
    std::sort(docks.begin(), docks.end(),
              [](const ps::components::DockContribution &a, const ps::components::DockContribution &b) {
                  return a.order < b.order;
              });
    for (const auto &spec : docks)
    {
        QWidget *created = spec.create ? spec.create(m_window) : nullptr;
        QDockWidget *dock = qobject_cast<QDockWidget *>(created);
        if (!dock && created)
        {
            dock = new QDockWidget(spec.title, m_window);
            dock->setWidget(created);
        }
        if (!dock)
            continue;
        if (!spec.id.isEmpty())
            dock->setObjectName(spec.id);
        if (!spec.title.isEmpty())
            dock->setWindowTitle(spec.title);
        m_window->addDockWidget(spec.area, dock);
        dock->setVisible(spec.visible);
    }

    if (auto *logDock = m_window->findChild<QDockWidget *>(QStringLiteral("dock.log")))
        m_window->resizeDocks({logDock}, {96}, Qt::Vertical);
    for (const auto &spec : registry.docks())
        if (spec.layout)
            spec.layout(m_window);
}

void DockManager::darkenExisting()
{
    for (auto *dock : m_window->findChildren<QDockWidget *>())
    {
        if (dock->titleBarWidget())
            continue;
        auto *bar = new DockTitleBar(m_window, dock);
        bar->setStyleSheet(QStringLiteral("background:#26303a;border-bottom:1px solid #3a4854;"));
        auto *lay = new QHBoxLayout(bar);
        lay->setContentsMargins(10, 0, 6, 0);
        lay->setSpacing(4);

        auto *icon = new QLabel(bar);
        icon->setPixmap(QIcon(dockIcon(dock)).pixmap(14, 14));
        icon->setStyleSheet(QStringLiteral("background:transparent;"));
        icon->setFixedSize(16, 16);
        lay->addWidget(icon);

        auto *title = new QLabel(dock->windowTitle(), bar);
        title->setStyleSheet(QStringLiteral("color:#e6edf3;font-size:13px;font-weight:600;background:transparent;"));
        lay->addWidget(title);
        lay->addStretch();

        auto *menuBtn = new QToolButton(bar);
        menuBtn->setText(QStringLiteral("▾"));
        menuBtn->setToolTip(QStringLiteral("最大化 / 最小化 / 浮动 / 关闭"));
        menuBtn->setCursor(Qt::PointingHandCursor);
        menuBtn->setFixedSize(32, 26);
        menuBtn->setStyleSheet(QStringLiteral(
            "QToolButton{background:#1b232b;border:1px solid #2d3841;border-radius:2px;color:#c9d1d9;"
            "padding:0;font-size:17px;font-weight:700;}"
            "QToolButton:hover{border-color:#f2b134;color:#f2b134;background:#222b33;}"));
        auto *menu = new QMenu(menuBtn);
        QAction *maximize = menu->addAction(QStringLiteral("最大化 / 还原"));
        QAction *left = menu->addAction(QStringLiteral("停靠到左侧"));
        QAction *right = menu->addAction(QStringLiteral("停靠到右侧"));
        QAction *bottom = menu->addAction(QStringLiteral("停靠到底部"));
        menu->addSeparator();
        QAction *autoHide = menu->addAction(QStringLiteral("最小化（自动隐藏）"));
        QAction *floatAct = menu->addAction(QStringLiteral("浮动 / 取消浮动"));
        menu->addSeparator();
        QAction *close = menu->addAction(QStringLiteral("关闭面板"));
        QObject::connect(left, &QAction::triggered, m_window, [this, dock] { toArea(dock, Qt::LeftDockWidgetArea); });
        QObject::connect(right, &QAction::triggered, m_window, [this, dock] { toArea(dock, Qt::RightDockWidgetArea); });
        QObject::connect(bottom, &QAction::triggered, m_window, [this, dock] { toArea(dock, Qt::BottomDockWidgetArea); });
        QObject::connect(maximize, &QAction::triggered, m_window, [this, dock] { toggleMax(dock); });
        QObject::connect(autoHide, &QAction::triggered, m_window, [this, dock] { restoreMax(); collapse(dock); });
        QObject::connect(floatAct, &QAction::triggered, m_window, [this, dock] {
            restoreMax();
            dock->setFloating(!dock->isFloating());
        });
        QObject::connect(close, &QAction::triggered, m_window, [this, dock] {
            restoreMax();
            dock->close();
        });
        menuBtn->setMenu(menu);
        QObject::connect(menuBtn, &QToolButton::clicked, menuBtn, [menuBtn, menu] { showDockMenu(menuBtn, menu); });
        lay->addWidget(menuBtn);
        bar->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(bar, &QWidget::customContextMenuRequested, bar, [bar, menu](const QPoint &pos) {
            menu->exec(bar->mapToGlobal(pos));
        });
        dock->setTitleBarWidget(bar);
    }
}

void DockManager::registerCommand(ps::commands::CommandManager &commands)
{
    commands.registerCommand(
        ps::commands::CommandSpec{QStringLiteral("view.toggleDock"), QStringLiteral("显示/隐藏面板"),
                                  {}, {}, true, {}, [this](const ps::commands::CommandContext &ctx) {
                                      return isVisibleById(ctx.arg);
                                  }},
        [this](const ps::commands::CommandContext &ctx) { toggleById(ctx.arg); });
}

bool DockManager::isVisibleById(const QString &id) const
{
    if (!m_window)
        return false;
    if (auto *dock = m_window->findChild<QDockWidget *>(id))
        return dock->isVisible();
    return false;
}

void DockManager::toggleById(const QString &id)
{
    if (auto *dock = m_window->findChild<QDockWidget *>(id))
        dock->setVisible(!dock->isVisible());
}

void DockManager::repositionHandles()
{
    if (!m_window)
        return;
    QHash<int, int> totals, seen;
    for (auto it = g_handles.begin(); it != g_handles.end(); ++it)
        if (it.value() && it.value()->isVisible())
            ++totals[static_cast<int>(m_window->dockWidgetArea(it.key()))];
    for (auto it = g_handles.begin(); it != g_handles.end(); ++it)
    {
        if (!it.value() || !it.value()->isVisible())
            continue;
        const int area = static_cast<int>(m_window->dockWidgetArea(it.key()));
        const int idx = seen[area]++;
        positionHandle(it.key(), it.value(), m_window, idx, qMax(1, totals.value(area, 1)));
    }
}

QWidget *DockManager::createTitleBar(QDockWidget *dock)
{
    return new DockTitleBar(m_window, dock);
}

void DockManager::toArea(QDockWidget *dock, Qt::DockWidgetArea area)
{
    restoreMax();
    dockToArea(dock, m_window, area);
}

void DockManager::collapse(QDockWidget *dock)
{
    if (!dock || !m_window)
        return;
    QWidget *button = g_handles.value(dock);
    if (!button)
    {
        auto *tab = new DockAutoHideTab(m_window, dock->windowTitle(), m_window->dockWidgetArea(dock),
            [this, dock] {
                dock->show();
                dock->raise();
                if (auto *h = g_handles.value(dock))
                    h->hide();
                repositionHandles();
            });
        QObject::connect(dock, &QDockWidget::visibilityChanged, tab, [tab](bool visible) {
            if (visible)
                tab->hide();
        });
        g_handles.insert(dock, tab);
        button = tab;
    }
    dock->hide();
    button->show();
    repositionHandles();
}

void DockManager::showMenu(QToolButton *button, QMenu *menu)
{
    showDockMenu(button, menu);
}

void DockManager::toggleMax(QDockWidget *dock)
{
    if (!dock)
        return;
    if (m_maxDock == dock)
    {
        restoreMax();
        return;
    }
    restoreMax();
    if (dock->isFloating())
    {
        const Qt::DockWidgetArea area = m_window->dockWidgetArea(dock);
        dock->setFloating(false);
        m_window->addDockWidget(area, dock);
    }
    m_maxCentral = m_window->centralWidget();
    if (m_maxCentral)
        m_maxCentral->hide();
    m_maxHidden.clear();
    for (QDockWidget *other : m_window->findChildren<QDockWidget *>())
        if (other != dock && other->isVisible())
        {
            m_maxHidden.append(other);
            other->hide();
        }
    dock->show();
    dock->raise();
    m_maxDock = dock;
}

void DockManager::restoreMax()
{
    if (!m_maxDock)
        return;
    for (QDockWidget *dock : m_maxHidden)
        if (dock)
            dock->show();
    m_maxHidden.clear();
    if (m_maxCentral)
        m_maxCentral->show();
    m_maxDock = nullptr;
}
