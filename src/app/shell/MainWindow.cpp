#include "shell/MainWindow.h"
#include "loader/DialogEngine.h"
#include "loader/LoaderOptionsDialog.h"
#include "datastore/DatasetCache.h"
#include "loader/LoaderRegistry.h"

#include <QAction>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSpinBox>
#include <QToolButton>
#include <QFileDialog>
#include <QTimer>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QMenu>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QShortcut>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QCloseEvent>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QIcon>
#include <QResizeEvent>
#include <QStyle>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <QPixmap>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QLineEdit>
#include <QEvent>
#include <QMouseEvent>
#include <QDrag>
#include <QMimeData>
#include <QDropEvent>
#include <QListWidget>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "plot/CanvasWidget.h"
#include "script/ScriptEngine.h"
#include "ps_kernel.h"
#include "transform/TransformDialog.h"

namespace {

// Ribbon 里的图表选择卡：用缩略图表达图型，比一排纯文字按钮更接近 Office/MATLAB 的样式库
void compactValueTable(QTableWidget *t)
{
    if (!t)
        return;
    t->setAlternatingRowColors(false);
    t->setWordWrap(false);
    t->verticalHeader()->setVisible(false);
    t->verticalHeader()->setDefaultSectionSize(21);
    t->verticalHeader()->setMinimumSectionSize(21);
    t->horizontalHeader()->setFixedHeight(23);
    QFont f = t->font();
    f.setPixelSize(12);
    t->setFont(f);
}

QPixmap plotThumb(const QString &kind)
{
    QPixmap pm(76, 38);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor("#2d3841"), 1));
    p.setBrush(QColor("#0b0f13"));
    p.drawRoundedRect(QRectF(0.5, 0.5, 75, 37), 3, 3);
    const QRectF r(7, 6, 62, 26);
    p.setPen(QPen(QColor("#26313a"), 1));
    p.drawLine(r.bottomLeft(), r.bottomRight());
    p.drawLine(r.bottomLeft(), r.topLeft());

    if (kind == QLatin1String("timeseries"))
    {
        QPainterPath path;
        for (int i = 0; i <= 40; ++i)
        {
            const double x = r.left() + r.width() * i / 40.0;
            const double y = r.center().y() - std::sin(i * 0.28) * 8.0;
            if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
        }
        p.setPen(QPen(QColor("#4ea1ff"), 1.6));
        p.drawPath(path);
    }
    else if (kind == QLatin1String("xy"))
    {
        for (int i = 0; i < 9; ++i)
        {
            const double x = r.left() + 6 + (i * 13) % 50;
            const double y = r.bottom() - 3 - (i * 7) % 20;
            p.setPen(QPen(QColor("#ffb454"), 1));
            p.setBrush(QColor("#ffb454"));
            p.drawEllipse(QPointF(x, y), 1.8, 1.8);
        }
    }
    else if (kind == QLatin1String("scatter"))
    {
        for (int i = 0; i < 14; ++i)
        {
            const double x = r.left() + 5 + (i * 17) % 52;
            const double y = r.bottom() - 2 - (i * 11) % 21;
            p.setPen(Qt::NoPen);
            p.setBrush(i % 3 ? QColor("#7ee787") : QColor("#ffb454"));
            p.drawEllipse(QPointF(x, y), 1.5, 1.5);
        }
    }
    else if (kind == QLatin1String("bar"))
    {
        for (int i = 0; i < 5; ++i)
        {
            const double h = 4 + (i * 7) % 19;
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#7ee787"));
            p.drawRect(QRectF(r.left() + 5 + i * 11, r.bottom() - h, 6, h));
        }
    }
    else if (kind == QLatin1String("histogram"))
    {
        for (int i = 0; i < 7; ++i)
        {
            const double h = 3 + (i * 5 + i * i) % 21;
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#d2a8ff"));
            p.drawRect(QRectF(r.left() + 4 + i * 8, r.bottom() - h, 6, h));
        }
    }
    else if (kind == QLatin1String("fft"))
    {
        QPainterPath path;
        for (int i = 0; i <= 32; ++i)
        {
            const double x = r.left() + r.width() * i / 32.0;
            const double env = std::exp(-i * 0.12);
            const double y = r.bottom() - 2 - std::fabs(std::sin(i * 0.55)) * 18.0 * env;
            if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
        }
        p.setPen(QPen(QColor("#39c5cf"), 1.4));
        p.drawPath(path);
    }
    else if (kind == QLatin1String("traj3d"))
    {
        p.setPen(QPen(QColor("#26313a"), 1));
        p.drawLine(QPointF(12, 28), QPointF(32, 21));
        p.drawLine(QPointF(32, 21), QPointF(58, 27));
        p.drawLine(QPointF(58, 27), QPointF(58, 9));
        p.drawLine(QPointF(58, 9), QPointF(36, 8));
        p.drawLine(QPointF(36, 8), QPointF(12, 14));
        p.drawLine(QPointF(12, 14), QPointF(12, 28));
        QPainterPath path;
        path.moveTo(14, 25);
        path.cubicTo(24, 8, 40, 12, 54, 20);
        p.setPen(QPen(QColor("#f2b134"), 1.6));
        p.drawPath(path);
    }
    else if (kind == QLatin1String("surface3d"))
    {
        p.setPen(QPen(QColor("#2a3b48"), 1));
        for (int i = 0; i < 6; ++i)
        {
            p.drawLine(QPointF(12 + i * 9, 25), QPointF(22 + i * 9, 31));
            p.drawLine(QPointF(12 + i * 9, 25), QPointF(34 + i * 9, 9));
        }
        QPainterPath path;
        path.moveTo(12, 22);
        path.cubicTo(26, 8, 44, 27, 64, 13);
        p.setPen(QPen(QColor("#39c5cf"), 1.4));
        p.drawPath(path);
    }
    else if (kind == QLatin1String("horizon"))
    {
        p.setPen(QPen(QColor("#4a5a68"), 1));
        p.setBrush(QColor("#2b3a4a"));
        p.drawEllipse(QPointF(38, 19), 14, 14);
        p.setBrush(QColor("#3a2f22"));
        p.drawChord(QRectF(24, 5, 28, 28), 180 * 16, 180 * 16);
        p.setPen(QPen(QColor("#f2b134"), 1.4));
        p.drawLine(QPointF(25, 19), QPointF(51, 19));
        p.drawLine(QPointF(38, 17), QPointF(38, 21));
        p.drawLine(QPointF(31, 19), QPointF(45, 19));
    }
    else if (kind == QLatin1String("compass"))
    {
        p.setPen(QPen(QColor("#4a5a68"), 1));
        p.drawEllipse(QPointF(38, 19), 13, 13);
        p.drawText(QRectF(35, 5, 8, 8), Qt::AlignCenter, QStringLiteral("N"));
        p.setPen(QPen(QColor("#ff7b72"), 1.8));
        p.drawLine(QPointF(38, 19), QPointF(43, 10));
        p.setPen(QPen(QColor("#c9d1d9"), 1));
        p.drawLine(QPointF(38, 19), QPointF(34, 27));
    }
    else if (kind == QLatin1String("gauge"))
    {
        p.setPen(QPen(QColor("#4a5a68"), 1.2));
        p.drawArc(QRectF(18, 7, 40, 34), 200 * 16, -220 * 16);
        p.setPen(QPen(QColor("#f2b134"), 1.6));
        p.drawLine(QPointF(38, 23), QPointF(51, 13));
        p.setBrush(QColor("#f2b134"));
        p.drawEllipse(QPointF(38, 23), 2, 2);
    }
    return pm;
}

QPixmap sourceThumb(const QString &kind)
{
    QPixmap pm(76, 38);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor("#2d3841"), 1));
    p.setBrush(QColor("#0b0f13"));
    p.drawRoundedRect(QRectF(0.5, 0.5, 75, 37), 3, 3);
    const QColor accent = kind == QLatin1String("ulog")     ? QColor("#4ea1ff")
                       : kind == QLatin1String("bin")      ? QColor("#ffb454")
                       : kind == QLatin1String("csv")      ? QColor("#7ee787")
                       : kind == QLatin1String("can")      ? QColor("#d2a8ff")
                       : kind == QLatin1String("mavlink")  ? QColor("#39c5cf")
                       : kind == QLatin1String("ros2")     ? QColor("#ff7b72")
                                                          : QColor("#f2b134");
    p.setPen(Qt::NoPen);
    p.setBrush(accent);
    p.drawRect(QRectF(0, 0, 3, 38));
    p.setPen(QPen(accent, 1.2));
    QPainterPath path;
    for (int i = 0; i <= 28; ++i)
    {
        const double x = 10 + i * 2.0;
        const double y = 19 - std::sin(i * 0.42) * (kind == QLatin1String("can") ? 6.0 : 8.0);
        if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
    }
    p.drawPath(path);
    p.setPen(QColor("#c9d1d9"));
    QFont f = p.font();
    f.setPixelSize(9);
    f.setBold(true);
    p.setFont(f);
    p.drawText(QRectF(9, 3, 60, 10), Qt::AlignLeft | Qt::AlignVCenter,
               kind == QLatin1String("ulog") ? QStringLiteral("ULog")
               : kind == QLatin1String("bin") ? QStringLiteral("BIN")
               : kind == QLatin1String("csv") ? QStringLiteral("CSV")
               : kind == QLatin1String("can") ? QStringLiteral("CAN")
               : kind == QLatin1String("mavlink") ? QStringLiteral("MAV")
               : kind == QLatin1String("ros2") ? QStringLiteral("ROS2")
                                               : QStringLiteral("RT"));
    return pm;
}

} // namespace
#include "SARibbonBar.h"
#include "SARibbonCategory.h"
#include "SARibbonPanel.h"
#include "SARibbonButtonGroupWidget.h"
#include "SARibbonGallery.h"
#include "SARibbonGalleryGroup.h"
#include "SARibbonThemeManager.h"
#include "SARibbonToolButton.h"

static void darkenDocks(MainWindow *w);
namespace { void dockToArea(QDockWidget *dock, MainWindow *win, Qt::DockWidgetArea area); }

// 只重写 sizeHint 的标题栏：QDockWidget 按 sizeHint 预留高度，固定 height 不算数，
// 32px 的 bar 只留 24px 空间就会压住正文第一行。

class DockTitleBar : public QWidget
{
public:
    explicit DockTitleBar(MainWindow *win, QDockWidget *dock)
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
            return; // 扔到窗口外：保持浮动
        const int dl = qAbs(gp.x() - winRect.left());
        const int dr = qAbs(gp.x() - winRect.right());
        const int db = qAbs(gp.y() - winRect.bottom());
        const int dt = qAbs(gp.y() - winRect.top());
        const int minD = std::min(std::min(dl, dr), std::min(db, dt));
        if (minD > 150)
            return; // 中间区域：保持浮动，不强行吸边
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
    MainWindow *m_win = nullptr;
    QDockWidget *m_dock = nullptr;
    QPoint m_pressPos;
    QPoint m_offset;
    bool m_dragging = false;
};

// Visual Studio 式自动隐藏页签：收成一条竖直窄条，标题竖排，点一下展开
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

namespace {

QHash<QDockWidget *, QWidget *> g_dockMinHandles;

void positionDockHandle(QDockWidget *dock, QWidget *btn, MainWindow *win, int index = 0, int total = 1)
{
    if (!dock || !btn || !win)
        return;
    const QRect cg = win->centralWidget()->geometry();
    const Qt::DockWidgetArea area = win->dockWidgetArea(dock);
    const int W = 26, GAP = 6;
    const int H = qMax(96, btn->height());
    if (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea)
    {
        if (auto *tab = dynamic_cast<DockAutoHideTab *>(btn))
            tab->setArea(area);
        const int x = area == Qt::LeftDockWidgetArea ? cg.left() + 2 : cg.right() - W - 2;
        const int block = total * H + (total - 1) * GAP;
        const int y = cg.center().y() - block / 2 + index * (H + GAP);
        btn->move(x, y);
    }
    else
    {
        if (auto *tab = dynamic_cast<DockAutoHideTab *>(btn))
            tab->setArea(area);
        const int y = area == Qt::TopDockWidgetArea ? cg.top() + 2 : cg.bottom() - W - 2;
        const int block = total * H + (total - 1) * GAP;
        const int x = cg.center().x() - block / 2 + index * (H + GAP);
        btn->move(x, y);
    }
    btn->raise();
}

void repositionDockHandles(MainWindow *win)
{
    if (!win)
        return;
    QHash<int, int> totals, seen;
    for (auto it = g_dockMinHandles.begin(); it != g_dockMinHandles.end(); ++it)
        if (it.value() && it.value()->isVisible())
            ++totals[static_cast<int>(win->dockWidgetArea(it.key()))];
    for (auto it = g_dockMinHandles.begin(); it != g_dockMinHandles.end(); ++it)
    {
        if (!it.value() || !it.value()->isVisible())
            continue;
        const int area = static_cast<int>(win->dockWidgetArea(it.key()));
        const int idx = seen[area]++;
        positionDockHandle(it.key(), it.value(), win, idx, qMax(1, totals.value(area, 1)));
    }
}

void collapseDock(QDockWidget *dock, MainWindow *win)
{
    if (!dock || !win)
        return;
    QWidget *btn = g_dockMinHandles.value(dock);
    if (!btn)
    {
        auto *tab = new DockAutoHideTab(win, dock->windowTitle(), win->dockWidgetArea(dock),
            [dock, win] {
                dock->show();
                dock->raise();
                if (auto *h = g_dockMinHandles.value(dock))
                    h->hide();
                repositionDockHandles(win);
            });
        QObject::connect(dock, &QDockWidget::visibilityChanged, tab, [tab](bool visible) {
            if (visible)
                tab->hide();
        });
        g_dockMinHandles.insert(dock, tab);
        btn = tab;
    }
    dock->hide();
    btn->show();
    repositionDockHandles(win);
}

void dockToArea(QDockWidget *dock, MainWindow *win, Qt::DockWidgetArea area)
{
    if (!dock || !win)
        return;
    const bool floating = dock->isFloating();
    if (floating)
        dock->setFloating(false);
    win->removeDockWidget(dock);
    win->addDockWidget(area, dock);
    dock->show();
    dock->raise();
}

// 页内 m×n 网格：QSplitter 只能做 n×1 / 1×n，这里补真正的二维布局
class PlotGrid : public QWidget
{
public:
    explicit PlotGrid(QWidget *parent = nullptr) : QWidget(parent), m_grid(new QGridLayout(this))
    {
        m_grid->setContentsMargins(0, 0, 0, 0);
        m_grid->setSpacing(4);
    }

    void setShape(int rows, int cols)
    {
        m_rows = std::clamp(rows, 1, 8);
        m_cols = std::clamp(cols, 1, 8);
        while (m_canvases.size() > m_rows * m_cols)
            ++m_rows;
        relayout();
    }

    void addCanvas(CanvasWidget *c)
    {
        if (!c || m_canvases.contains(c))
            return;
        c->setParent(this);
        c->show();
        m_canvases.append(c);
        while (m_canvases.size() > m_rows * m_cols)
            ++m_rows;
        relayout();
    }

    void removeCanvas(CanvasWidget *c)
    {
        if (!c)
            return;
        m_canvases.removeOne(c);
        m_grid->removeWidget(c);
        relayout();
    }

    QVector<CanvasWidget *> canvases() const { return m_canvases; }
    int rows() const { return m_rows; }
    int cols() const { return m_cols; }

private:
    void relayout()
    {
        while (QLayoutItem *item = m_grid->takeAt(0))
            delete item;
        for (int i = 0; i < m_canvases.size(); ++i)
            m_grid->addWidget(m_canvases.at(i), i / m_cols, i % m_cols);
        for (int r = 0; r < 8; ++r)
            m_grid->setRowStretch(r, r < m_rows ? 1 : 0);
        for (int c = 0; c < 8; ++c)
            m_grid->setColumnStretch(c, c < m_cols ? 1 : 0);
    }

    QGridLayout *m_grid = nullptr;
    QVector<CanvasWidget *> m_canvases;
    int m_rows = 1;
    int m_cols = 1;
};

void collectPageCanvases(QWidget *root, QVector<CanvasWidget *> &out)
{
    if (!root)
        return;
    if (auto *c = dynamic_cast<CanvasWidget *>(root))
    {
        out.append(c);
        return;
    }
    if (auto *g = dynamic_cast<PlotGrid *>(root))
    {
        out += g->canvases();
        return;
    }
    const auto kids = root->findChildren<CanvasWidget *>();
    for (CanvasWidget *c : kids)
        out.append(c);
}


QWidget *buildGridSplitter(const QVector<CanvasWidget *> &canvases, int rows, int cols, QWidget *parent)
{
    rows = std::clamp(rows, 1, 8);
    cols = std::clamp(cols, 1, 8);
    auto *root = new QSplitter(Qt::Vertical, parent);
    root->setHandleWidth(5);
    root->setChildrenCollapsible(false);
    for (int r = 0; r < rows; ++r)
    {
        auto *row = new QSplitter(Qt::Horizontal, root);
        row->setHandleWidth(5);
        row->setChildrenCollapsible(false);
        for (int c = 0; c < cols; ++c)
        {
            const int i = r * cols + c;
            if (i < canvases.size())
                row->addWidget(canvases.at(i));
        }
        root->addWidget(row);
    }
    for (int r = 0; r < rows; ++r)
        root->setStretchFactor(r, 1);
    return root;
}

} // namespace

// 字段树拖拽：Item 里存的是内核通道指针，默认 QTreeWidget 的 MIME 带不过去，自己塞一条
class ChannelTree : public QTreeWidget
{
public:
    using QTreeWidget::QTreeWidget;

protected:
    void startDrag(Qt::DropActions) override
    {
        QTreeWidgetItem *item = currentItem();
        if (!item || item->childCount() > 0)
            return;
        const qulonglong ptr = item->data(0, Qt::UserRole).value<qulonglong>();
        if (!ptr)
            return;
        auto *mime = new QMimeData;
        mime->setData(QStringLiteral("application/x-plotscope-channel"), QByteArray::number(ptr));
        auto *drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->exec(Qt::CopyAction);
    }
};

// 源行名称按可用宽度省略中部，避免把右侧操作图标挤出去
class ElideLabel : public QLabel
{
public:
    ElideLabel(const QString &full, QWidget *parent) : QLabel(full, parent), m_full(full) {}
protected:
    void resizeEvent(QResizeEvent *e) override
    {
        QLabel::resizeEvent(e);
        setText(fontMetrics().elidedText(m_full, Qt::ElideMiddle, width()));
    }
private:
    QString m_full;
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("PlotScope 0.1.0 — 绘图分析"));
    resize(1440, 880);


    setMenuWidget(createRibbon());
    createDocks();
    darkenDocks(this);
    createPages();

    // 内核连通性：状态栏直接读 ABI，证明 C ABI 已经打通
    const QString kernelInfo =
        QStringLiteral("内核 ps-abi %1 · ABI v%2 · selftest %3")
            .arg(QString::fromUtf8(ps_kernel_version()))
            .arg(ps_abi_version())
            .arg(ps_abi_selftest() == 0 ? QStringLiteral("通过") : QStringLiteral("失败"));
    m_kernelInfo = kernelInfo;
    statusBar()->showMessage(kernelInfo);

    // PJ 习惯：Ctrl+S 存当前布局，Ctrl+Shift+O 打开布局
    auto *saveSc = new QShortcut(QKeySequence(QStringLiteral("Ctrl+S")), this);
    connect(saveSc, &QShortcut::activated, this, [this] { saveLayoutTo(defaultLayoutPath()); });
    auto *openSc = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+O")), this);
    connect(openSc, &QShortcut::activated, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("导入布局"), {},
            QStringLiteral("PlotScope 布局 (*.psl)"));
        if (!f.isEmpty())
            importLayoutFrom(f);
    });

    log(QStringLiteral("启动完成：%1").arg(kernelInfo));
}

QWidget *MainWindow::createRibbon()
{
    // Ribbon 结构层交给 SARibbon（MIT，vendored 在 src/3dparty/saribbon）：
    // 页签溢出、键盘导航、面板布局都是它的事；我们只负责配色与命令。
    auto *ribbon = new SARibbonBar(this);
    ribbon->setRibbonStyle(SARibbonBar::RibbonStyleLooseSingleRow);
    // 有 Ribbon 就不要再留一条同名 QMenuBar，两排"文件/数据/绘图"并排是最刺眼的不协调
    if (auto *mb = menuBar())
        mb->hide();
    ribbon->setTitleVisible(false);       // 窗口标题在 titlebar，不占 ribbon 高度
    // 标题带直接压成 0：留 30px 空行只会把页签往下推，组标题也被挤掉
    ribbon->setTitleBarHeight(0, true);
    ribbon->setTabBarHeight(26);
    // 收缩：右下角 chevron + 双击页签，都能把面板收起只留页签行
    ribbon->showMinimumModeButton(true);
    ribbon->setTabDoubleClickToMinimumMode(true);
    ribbon->setPanelTitleHeight(14);
    // 分组标题：vendored 布局把标题画在面板底边内，category 高度必须把它算进去
    ribbon->setEnableShowPanelTitle(true);
    ribbon->setPanelToolButtonIconSize(QSize(18, 18), QSize(26, 26));
    ribbon->setPanelSpacing(3);
    ribbon->setCategoryHeight(78, true); // 图标 23 + 内边距 + 分组标题 14，留出基线余量

    // 大按钮同样只留图标：分组标题已说明用途，图标下再叠一行字就是原型里最挤的部分
    auto addLarge = [](SARibbonPanel *panel, const QString &text, std::function<void()> fn, QObject *ctx) {
        auto *a = new QAction(text, ctx); // 先用全名建 action：图标按 text 查表
        a->setToolTip(text);
        a->setProperty("rbIconOnly", true); // Ribbon 以图标为主：文字只留 tooltip
        QObject::connect(a, &QAction::triggered, ctx, fn);
        panel->addLargeAction(a);
    };
    // 小按钮：只有 1~2 个字才显示文字，其余只留图标 + tooltip（图标 16px 撑得住）
    auto addSmall = [](SARibbonPanel *panel, const QString &text, std::function<void()> fn, QObject *ctx) {
        auto *a = new QAction(text, ctx);
        a->setToolTip(text);
        a->setProperty("rbIconOnly", true);
        QObject::connect(a, &QAction::triggered, ctx, fn);
        panel->addSmallAction(a);
    };

    // 文件
    {
        auto *cat = ribbon->addCategoryPage(QStringLiteral("文件"));
        auto *pLayout = cat->addPanel(QStringLiteral("文件"));
        addLarge(pLayout, QStringLiteral("打开文件"), [this] {
            const QStringList fs = QFileDialog::getOpenFileNames(this, QStringLiteral("打开文件"), {},
                QStringLiteral("飞行日志 (*.ulg *.bin *.csv *.asc *.blf);;全部 (*)"));
            for (const QString &f : fs)
                loadFile(f);
        }, this);
        addLarge(pLayout, QStringLiteral("导入布局"), [this] {
            const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("导入布局"), {},
                QStringLiteral("PlotScope 布局 (*.psl)"));
            if (!f.isEmpty())
                importLayoutFrom(f);
        }, this);
        addLarge(pLayout, QStringLiteral("设置"), [this] { openSettings(); }, this);

        auto *pExport = cat->addPanel(QStringLiteral("导出"));
        addLarge(pExport, QStringLiteral("保存布局"), [this] {
            const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("导出布局"), {},
                QStringLiteral("PlotScope 布局 (*.psl)"));
            if (!f.isEmpty())
                saveLayoutTo(f);
        }, this);
        addSmall(pExport, QStringLiteral("导出设置"), [this] { log(QStringLiteral("导出设置：M1 接 YAML")); }, this);
        addSmall(pExport, QStringLiteral("导出数据"), [this] { log(QStringLiteral("导出数据：M1 接 CSV/NDF")); }, this);

        auto *pShot = cat->addPanel(QStringLiteral("截图"));
        addLarge(pShot, QStringLiteral("截图当前页"), [this] {
            QWidget *page = m_pages ? m_pages->currentWidget() : nullptr;
            if (!page)
                return;
            const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("截图另存为"), {},
                QStringLiteral("PNG (*.png)"));
            if (f.isEmpty())
                return;
            if (page->grab().save(f))
                log(QStringLiteral("已截图：%1").arg(f));
        }, this);
    }

    // 数据
    {
        auto *cat = ribbon->addCategoryPage(QStringLiteral("数据"));
        auto *pSrc = cat->addPanel(QStringLiteral("来源"));
        auto *srcGallery = pSrc->addGallery(true);
        QList<QAction *> srcActs;
        auto addSource = [&](const QString &kind, const QString &label, const QString &filter, bool implemented) {
            auto *a = new QAction(QIcon(sourceThumb(kind)), label, this);
            a->setToolTip(implemented ? QStringLiteral("选择 %1 文件（可多选）").arg(label)
                                      : QStringLiteral("%1 解析尚未接入").arg(label));
            connect(a, &QAction::triggered, this, [this, label, filter, implemented] {
                if (!implemented)
                {
                    log(QStringLiteral("%1：解析器尚未接入（M1/M2）").arg(label));
                    QMessageBox::information(this, QStringLiteral("数据源"),
                                             QStringLiteral("%1 解析尚未接入，当前可用 ULog / DataFlash / CSV / CAN。")
                                                 .arg(label));
                    return;
                }
                const QStringList fs = QFileDialog::getOpenFileNames(this, label, {}, filter);
                for (const QString &f : fs)
                    loadFile(f);
            });
            srcActs.append(a);
        };
        addSource(QStringLiteral("ulog"), QStringLiteral("ULog"),
                  QStringLiteral("ULog (*.ulg);;全部 (*)"), true);
        addSource(QStringLiteral("bin"), QStringLiteral("DataFlash"),
                  QStringLiteral("ArduPilot BIN (*.bin);;全部 (*)"), true);
        addSource(QStringLiteral("csv"), QStringLiteral("CSV"),
                  QStringLiteral("CSV (*.csv);;全部 (*)"), true);
        addSource(QStringLiteral("can"), QStringLiteral("CAN"),
                  QStringLiteral("CAN 日志 (*.asc *.blf);;全部 (*)"), true);
        addSource(QStringLiteral("mavlink"), QStringLiteral("MAVLink"), QString(), false);
        addSource(QStringLiteral("ros2"), QStringLiteral("ROS2"), QString(), false);
        addSource(QStringLiteral("rt"), QStringLiteral("实时流"), QString(), false);
        auto *srcGroup = srcGallery->addCategoryActions(QStringLiteral("数据源类型"), srcActs);
        srcGroup->setGalleryGroupStyle(SARibbonGalleryGroup::IconWithText);
        srcGroup->setDisplayRow(SARibbonGalleryGroup::DisplayOneRow);
        srcGroup->setIconSize(QSize(66, 32));
        srcGroup->setGridMinimumWidth(76);
        srcGroup->setGridMaximumWidth(96);
        srcGallery->setSingleRowMode(true);
        srcGallery->setMinimumWidth(600);
        srcGallery->setMaximumWidth(600);
        srcGallery->setCurrentViewGroup(srcGroup);

        auto *pQuick = cat->addPanel(QStringLiteral("工作区"));
        addLarge(pQuick, QStringLiteral("快速打开"), [this] {
            const QStringList fs = QFileDialog::getOpenFileNames(this, QStringLiteral("快速打开"), {},
                QStringLiteral("飞行日志 (*.ulg *.bin *.csv *.asc *.blf);;全部 (*)"));
            for (const QString &f : fs)
                loadFile(f);
        }, this);
        addSmall(pQuick, QStringLiteral("最近"), [this] {
            QMenu menu(this);
            QSettings st(QStringLiteral("NextPilot"), QStringLiteral("PlotScope"));
            const QStringList recent = st.value(QStringLiteral("recentFiles")).toStringList();
            if (recent.isEmpty())
            {
                QAction *empty = menu.addAction(QStringLiteral("暂无最近文件"));
                empty->setEnabled(false);
            }
            for (const QString &path : recent)
            {
                QAction *a = menu.addAction(QFileInfo(path).fileName());
                a->setToolTip(path);
                connect(a, &QAction::triggered, this, [this, path] { loadFile(path); });
            }
            menu.exec(QCursor::pos());
        }, this);
        addSmall(pQuick, QStringLiteral("导出快照"), [this] { exportActiveCsv(); }, this);
        addSmall(pQuick, QStringLiteral("重载"), [this] {
            SourceData *src = sourceById(m_activeSource);
            if (!src || src->path.isEmpty())
            {
                log(QStringLiteral("当前源没有可重载的文件"));
                return;
            }
            // 先摘掉旧行/数据集，再按同一路径重新解析，避免出现重复源
            const QString path = src->path;
            QWidget *row = src->row;
            if (row)
                removeSourceRow(row);
            loadFile(path, false);
        }, this);
    }

    // 绘图 + 仪表合并：像 Office/MATLAB 的图表样式库，缩略图直接表达图型
    {
        auto *cat = ribbon->addCategoryPage(QStringLiteral("绘图"));
        auto *pType = cat->addPanel(QStringLiteral("图表"));
        auto *plotGallery = pType->addGallery(true);
        QList<QAction *> plotActs;
        auto addPlot = [&](const QString &kind, const QString &label, const QString &tip) {
            auto *a = new QAction(QIcon(plotThumb(kind)), label, this);
            a->setToolTip(tip);
            connect(a, &QAction::triggered, this, [this, kind, label] {
                if (kind == QLatin1String("timeseries"))
                {
                    newPage();
                    if (auto *c = currentCanvas())
                    {
                        c->setTitle(QStringLiteral("%1 · 选择通道").arg(label));
                        log(QStringLiteral("新建图页：%1").arg(label));
                    }
                }
                else
                {
                    auto *c = new CanvasWidget(this);
                    c->clearChannels();
                    c->setTitle(label);
                    CanvasWidget::PlotMode mode = CanvasWidget::Xy;
                    if (kind == QLatin1String("scatter")) mode = CanvasWidget::Scatter;
                    else if (kind == QLatin1String("bar")) mode = CanvasWidget::BarChart;
                    else if (kind == QLatin1String("histogram")) mode = CanvasWidget::Histogram;
                    else if (kind == QLatin1String("fft")) mode = CanvasWidget::Spectrum;
                    else if (kind == QLatin1String("horizon")) mode = CanvasWidget::Horizon;
                    else if (kind == QLatin1String("compass")) mode = CanvasWidget::Compass;
                    else if (kind == QLatin1String("gauge")) mode = CanvasWidget::Gauge;
                    else if (kind == QLatin1String("traj3d")) mode = CanvasWidget::Traj3D;
                    else if (kind == QLatin1String("surface3d")) mode = CanvasWidget::Xy;
                    c->setPlotMode(mode, kind);
                    CanvasWidget *seed = nullptr;
                    int best = 0;
                    for (CanvasWidget *w : findChildren<CanvasWidget *>())
                    {
                        if (w == c)
                            continue;
                        const int n = w->sampleSeries().size();
                        if (n > best)
                        {
                            best = n;
                            seed = w;
                        }
                    }
                    if (seed)
                        for (const CanvasWidget::SampleSeries &s : seed->sampleSeries())
                            c->addGeneratedSeries(s.name, s.time, s.value);
                    setupCanvas(c);
                    const int plusIdx = m_pages->count() - 1;
                    m_pages->insertTab(plusIdx, c, label);
                    m_pages->setCurrentWidget(c);
                    log(QStringLiteral("新建图页：%1").arg(label));
                }
            });
            plotActs.append(a);
        };
        addPlot(QStringLiteral("timeseries"), QStringLiteral("时间序列"), QStringLiteral("时间轴曲线，当前已实现"));
        addPlot(QStringLiteral("xy"), QStringLiteral("XY 曲线"), QStringLiteral("X-Y 双通道曲线"));
        addPlot(QStringLiteral("scatter"), QStringLiteral("散点图"), QStringLiteral("二维散点"));
        addPlot(QStringLiteral("bar"), QStringLiteral("柱状图"), QStringLiteral("离散量柱状对比"));
        addPlot(QStringLiteral("histogram"), QStringLiteral("直方图"), QStringLiteral("样本分布"));
        addPlot(QStringLiteral("fft"), QStringLiteral("频谱 FFT"), QStringLiteral("幅值频谱"));
        addPlot(QStringLiteral("traj3d"), QStringLiteral("3D 轨迹"), QStringLiteral("三维空间轨迹"));
        addPlot(QStringLiteral("surface3d"), QStringLiteral("3D 曲面"), QStringLiteral("三维曲面 / 频谱"));
        addPlot(QStringLiteral("horizon"), QStringLiteral("地平仪"), QStringLiteral("姿态地平仪"));
        addPlot(QStringLiteral("compass"), QStringLiteral("罗盘"), QStringLiteral("航向罗盘"));
        addPlot(QStringLiteral("gauge"), QStringLiteral("表盘"), QStringLiteral("单值仪表盘"));
        auto *plotGroup = plotGallery->addCategoryActions(QStringLiteral("图表类型"), plotActs);
        plotGroup->setGalleryGroupStyle(SARibbonGalleryGroup::IconWithText);
        plotGroup->setDisplayRow(SARibbonGalleryGroup::DisplayOneRow);
        plotGroup->setIconSize(QSize(66, 32));
        plotGroup->setGridMinimumWidth(74);
        plotGroup->setGridMaximumWidth(94);
        plotGallery->setSingleRowMode(true);
        plotGallery->setMinimumWidth(860);
        plotGallery->setMaximumWidth(860);
        plotGallery->setCurrentViewGroup(plotGroup);

        auto *pPage = cat->addPanel(QStringLiteral("页"));
        addLarge(pPage, QStringLiteral("新建页"), [this] { newPage(); }, this);
        addLarge(pPage, QStringLiteral("添加画布"), [this] { addCanvasToPage(); }, this);
        addLarge(pPage, QStringLiteral("向右拆分"), [this] {
            if (auto *c = currentCanvas())
                splitCanvas(c, Qt::Horizontal);
        }, this);
        addLarge(pPage, QStringLiteral("向下拆分"), [this] {
            if (auto *c = currentCanvas())
                splitCanvas(c, Qt::Vertical);
        }, this);

        // 页内布局：MATLAB subplot / PJ 的 m×n
        auto *pGrid = cat->addPanel(QStringLiteral("布局"));
        auto *gridBox = new QWidget(this);
        auto *gridLay = new QHBoxLayout(gridBox);
        gridLay->setContentsMargins(2, 0, 2, 0);
        gridLay->setSpacing(2);
        auto *rowsSpin = new QSpinBox(gridBox);
        rowsSpin->setRange(1, 8);
        rowsSpin->setValue(2);
        rowsSpin->setPrefix(QStringLiteral("行 "));
        rowsSpin->setFixedWidth(58);
        auto *colsSpin = new QSpinBox(gridBox);
        colsSpin->setRange(1, 8);
        colsSpin->setValue(1);
        colsSpin->setPrefix(QStringLiteral("列 "));
        colsSpin->setFixedWidth(58);
        auto *applyGrid = new QToolButton(gridBox);
        applyGrid->setText(QStringLiteral("应用"));
        applyGrid->setToolTip(QStringLiteral("按行×列重排当前页画布"));
        applyGrid->setCursor(Qt::PointingHandCursor);
        applyGrid->setStyleSheet(QStringLiteral(
            "QToolButton{background:#1b232b;border:1px solid #2d3841;color:#c9d1d9;font-size:11px;padding:3px 8px;}"
            "QToolButton:hover{border-color:#f2b134;color:#f2b134;}"));
        gridLay->addWidget(rowsSpin);
        gridLay->addWidget(colsSpin);
        gridLay->addWidget(applyGrid);
        connect(applyGrid, &QToolButton::clicked, this, [this, rowsSpin, colsSpin] {
            applyCanvasGrid(rowsSpin->value(), colsSpin->value());
        });
        pGrid->addWidget(gridBox, SARibbonPanelItem::Large);

        auto *pView = cat->addPanel(QStringLiteral("视图"));
        auto *aGrid = new QAction(QStringLiteral("网格"), this);
        auto *aBox = new QAction(QStringLiteral("边框"), this);
        auto *aLegend = new QAction(QStringLiteral("图例"), this);
        auto *aRubber = new QAction(QStringLiteral("框选"), this);
        for (QAction *a : {aGrid, aBox, aLegend, aRubber})
        {
            a->setCheckable(true);
            a->setToolTip(a->text());
            pView->addSmallAction(a);
        }
        const QVector<CanvasWidget *> initial = pageCanvases();
        if (!initial.isEmpty())
        {
            aGrid->setChecked(initial.first()->gridVisible());
            aBox->setChecked(initial.first()->boxVisible());
            aLegend->setChecked(initial.first()->legendVisible());
            aRubber->setChecked(initial.first()->rubberZoomEnabled());
        }
        connect(aGrid, &QAction::toggled, this, [this](bool on) {
            for (CanvasWidget *c : pageCanvases()) c->setGridVisible(on);
        });
        connect(aBox, &QAction::toggled, this, [this](bool on) {
            for (CanvasWidget *c : pageCanvases()) c->setBoxVisible(on);
        });
        connect(aLegend, &QAction::toggled, this, [this](bool on) {
            for (CanvasWidget *c : pageCanvases()) c->setLegendVisible(on);
        });
        connect(aRubber, &QAction::toggled, this, [this](bool on) {
            for (CanvasWidget *c : pageCanvases()) c->setRubberZoomEnabled(on);
        });
        connect(m_pages, &QTabWidget::currentChanged, this,
                [this, aGrid, aBox, aLegend, aRubber](int) {
            const QVector<CanvasWidget *> cs = pageCanvases();
            const bool has = !cs.isEmpty();
            for (QAction *a : {aGrid, aBox, aLegend, aRubber})
                a->setEnabled(has);
            if (!has)
                return;
            aGrid->setChecked(cs.first()->gridVisible());
            aBox->setChecked(cs.first()->boxVisible());
            aLegend->setChecked(cs.first()->legendVisible());
            aRubber->setChecked(cs.first()->rubberZoomEnabled());
        });
        addSmall(pView, QStringLiteral("放大"), [this] {
            for (CanvasWidget *c : pageCanvases()) c->zoomBy(0.8);
        }, this);
        addSmall(pView, QStringLiteral("缩小"), [this] {
            for (CanvasWidget *c : pageCanvases()) c->zoomBy(1.25);
        }, this);
        addSmall(pView, QStringLiteral("自动 Y"), [this] {
            for (CanvasWidget *c : pageCanvases()) c->requestAutoY();
        }, this);
        addSmall(pView, QStringLiteral("复位"), [this] {
            for (CanvasWidget *c : pageCanvases()) c->requestAutoY();
        }, this);
    }

    // 脚本
    {
        auto *cat = ribbon->addCategoryPage(QStringLiteral("脚本"));
        auto *p = cat->addPanel(QStringLiteral("Lua"));
        addLarge(p, QStringLiteral("运行"), [this] {
            log(QStringLiteral("Lua 5.4 逐块执行（10 ms/块）：out = ps.movingAvg(ps.derivative(ps.ch(\"att.roll\")), 5)"));
        }, this);
        addSmall(p, QStringLiteral("格式化"), [this] { log(QStringLiteral("脚本格式化（stylua 规则，M2 接）")); }, this);
    }

    // 协议
    {
        auto *cat = ribbon->addCategoryPage(QStringLiteral("协议"));
        auto *p = cat->addPanel(QStringLiteral("编辑器"));
        addLarge(p, QStringLiteral("新建 Loader"), [this] {
            log(QStringLiteral("新建 Loader：打开附属编辑器（QDialog，D13）"));
        }, this);
        addSmall(p, QStringLiteral("帧模板 YAML"), [this] { log(QStringLiteral("帧模板：YAML 热编译（D6）")); }, this);
        addSmall(p, QStringLiteral("校验"), [this] { log(QStringLiteral("校验模板：字段偏移/位域越界检查（M1）")); }, this);
    }

    // 视图
    {
        auto *cat = ribbon->addCategoryPage(QStringLiteral("视图"));
        auto *p = cat->addPanel(QStringLiteral("面板"));
        auto toggle = [this](const char *obj) {
            if (auto *d = findChild<QDockWidget *>(QString::fromLatin1(obj)))
                d->setVisible(!d->isVisible());
        };
        addLarge(p, QStringLiteral("数据源"), [toggle] { toggle("dock.src"); }, this);
        addSmall(p, QStringLiteral("属性"), [toggle] { toggle("dock.prop"); }, this);
        addSmall(p, QStringLiteral("脚本"), [toggle] { toggle("dock.script"); }, this);
        addSmall(p, QStringLiteral("日志"), [toggle] { toggle("dock.log"); }, this);
    }

    // 右侧芯片位：只读状态摘要，跟随实际数据源刷新
    auto *rg = ribbon->rightButtonGroup();
    m_chipSource = new QAction(QStringLiteral("无数据"), this);
    m_chipLod = new QAction(QStringLiteral("LOD: Rust"), this);
    for (QAction *a : {m_chipSource, m_chipLod})
    {
        a->setEnabled(false);
        rg->addAction(a);
    }
    updateStatusChips();

    // 自绘 SVG 图标：没有图标时 SARibbon 会在按钮左侧留空方块，观感直接崩
    {
        static const QHash<QString, QString> kIcon = {
            {QStringLiteral("打开文件"), QStringLiteral("open")},
            {QStringLiteral("导入布局"), QStringLiteral("import")},
            {QStringLiteral("设置"), QStringLiteral("settings")},
            {QStringLiteral("导出布局"), QStringLiteral("export")},
            {QStringLiteral("导出设置"), QStringLiteral("export_cfg")},
            {QStringLiteral("导出数据"), QStringLiteral("export_data")},
            {QStringLiteral("截图当前页"), QStringLiteral("screenshot")},
            {QStringLiteral("打开数据源"), QStringLiteral("datasource")},
            {QStringLiteral("全部开始"), QStringLiteral("play")},
            {QStringLiteral("全部暂停"), QStringLiteral("pause")},
            {QStringLiteral("新建页"), QStringLiteral("newpage")},
            {QStringLiteral("添加画布"), QStringLiteral("plus")},
            {QStringLiteral("自动 Y"), QStringLiteral("autoy")},
            {QStringLiteral("游标"), QStringLiteral("cursor")},
            {QStringLiteral("复位视图"), QStringLiteral("reset")},
            {QStringLiteral("地平仪"), QStringLiteral("horizon")},
            {QStringLiteral("罗盘"), QStringLiteral("compass")},
            {QStringLiteral("表盘"), QStringLiteral("gauge")},
            {QStringLiteral("运行"), QStringLiteral("run")},
            {QStringLiteral("格式化"), QStringLiteral("format")},
            {QStringLiteral("新建 Loader"), QStringLiteral("loader")},
            {QStringLiteral("帧模板 YAML"), QStringLiteral("yaml")},
            {QStringLiteral("校验"), QStringLiteral("validate")},
            {QStringLiteral("数据源"), QStringLiteral("datasource")},
            {QStringLiteral("属性"), QStringLiteral("prop")},
            {QStringLiteral("脚本"), QStringLiteral("script")},
            {QStringLiteral("日志"), QStringLiteral("log")},
            {QStringLiteral("网格"), QStringLiteral("grid")},
            {QStringLiteral("边框"), QStringLiteral("box")},
            {QStringLiteral("图例"), QStringLiteral("legend")},
            {QStringLiteral("框选"), QStringLiteral("cursor")},
            {QStringLiteral("放大"), QStringLiteral("zoom_in")},
            {QStringLiteral("缩小"), QStringLiteral("zoom_out")},
            {QStringLiteral("向右拆分"), QStringLiteral("split_h")},
            {QStringLiteral("向下拆分"), QStringLiteral("split_v")},
            {QStringLiteral("最近"), QStringLiteral("import")},
            {QStringLiteral("导出快照"), QStringLiteral("export_data")},
            {QStringLiteral("重载"), QStringLiteral("reset")},
            {QStringLiteral("复位"), QStringLiteral("reset")},
        };
        // QAction 的 parent 是 this（不是 ribbon），按 ribbon 找会一个都拿不到
        for (auto *a : findChildren<QAction *>())
        {
            const auto it = kIcon.find(a->text());
            if (it != kIcon.end())
            {
                a->setIcon(QIcon(QStringLiteral(":/icons/%1.svg").arg(it.value())));
                if (a->property("rbIconOnly").toBool())
                    a->setText(QString());

            }
        }
    }

    if (auto *ab = ribbon->applicationButton())
        ab->hide(); // 左上角默认有个空按钮，留白很难看

    // 悬停提示改状态栏：QToolTip 会飘在按钮下方，正好盖住分组标题
    for (auto *b : ribbon->findChildren<SARibbonToolButton *>())
    {
        b->setToolTip(QString());
        if (auto *a = b->defaultAction())
        {
            b->setProperty("rbTip", a->toolTip());
            a->setToolTip(QString());
        }
        b->installEventFilter(this);
    }

    // 面板本体撑到 68 px：SARibbon 的 category 高度不会自动把 panel 拉满
    for (auto *p : ribbon->findChildren<SARibbonPanel *>())
    {
        p->setMinimumHeight(56);
        if (auto *l = p->layout())
            l->setContentsMargins(4, 4, 9, 4);
    }

    // 大按钮 54×54、小按钮单行 22：只用 QSS 分不开两种形态，先打属性标记
    for (auto *b : ribbon->findChildren<SARibbonToolButton *>())
    {
        b->setProperty("rbLarge", b->buttonType() == SARibbonToolButton::LargeButton);
    }

    // 静态库里资源要显式初始化，否则 dark 主题静默回退浅色 QSS
    Q_INIT_RESOURCE(SARibbonResource);
    SA::applyRibbonTheme(this, ribbon, SARibbonTheme::RibbonThemeDark);
    // 视觉对齐 design/prototype/PlotScope-工作区原型.html 的 ribbon 段：
    // 扁平、无边框、琥珀色选中，压掉 Office 味的蓝色和内边距
    ribbon->setStyleSheet(ribbon->styleSheet() + QStringLiteral(R"(
        SARibbonBar { background:#141a1f; border-bottom:1px solid #232b33; }
        SARibbonCategory { background:#141a1f; border:0; }
        SARibbonPanel { background:#141a1f; border:0; border-right:1px solid #1b232a; }
        SARibbonPanelLabel { color:#7d8b96; font-size:10px; background:transparent; padding-bottom:3px; }
        SARibbonToolButton {
            background:transparent; border:1px solid transparent; border-radius:0;
            color:#8494a0; font-size:12px; padding:0; margin:0;
        }
        SARibbonToolButton[rbLarge="true"] { min-width:52px; max-width:60px; min-height:48px; max-height:48px; }
        SARibbonToolButton[rbLarge="false"] { min-height:21px; max-height:22px; padding:2px 8px; }
        SARibbonToolButton:hover { background:#1b232a; color:#c8d2da; border-color:#222b33; }
        SARibbonToolButton:pressed { background:#212a31; }
        SARibbonToolButton:checked { background:#212a31; color:#f2b134; border-color:#222b33; }
        SARibbonToolButton:disabled { color:#4a545d; }
        SARibbonToolButton::menu-indicator { image:none; width:0; }
        SARibbonSeparatorWidget { background:#1b232a; }
        SARibbonPanelOptionButton { background:transparent; border:0; }
        SARibbonPanelOptionButton:hover { background:#1b232a; }
        #objSARibbonTabBar { background:#141a1f; border:0; }
        #objSARibbonTabBar::tab {
            background:transparent; color:#5d6c77; font-size:11px;
            padding:0 11px; margin:0 1px; border:0; border-bottom:2px solid transparent;
            min-width:0;
        }
        #objSARibbonTabBar::tab:hover { color:#c8d2da; background:#1a2127; }
        #objSARibbonTabBar::tab:selected { color:#f2b134; border-bottom:2px solid #f2b134; }
        #objSARibbonQuickAccessBar { background:#141a1f; }
    )"));
    return ribbon;
}
void MainWindow::createDocks()
{
    // 左：数据源（对齐原型 d-src）：源列表 → 字段/参数/消息 三页签 → 内容
    auto *srcDock = new QDockWidget(QStringLiteral("数据源"), this);
    srcDock->setObjectName(QStringLiteral("dock.src"));

    // 标题栏：标题 + 汇总 + 全局起停 + 关闭，全部放一行，正文从源列表直接开始（不再叠第二行头部）
    auto *srcBar = new DockTitleBar(this, srcDock);
    srcBar->setStyleSheet(QStringLiteral(
        "background:#26303a;border-bottom:1px solid #3a4854;"));
    auto *barLay = new QHBoxLayout(srcBar);
    barLay->setContentsMargins(10, 0, 6, 0);
    barLay->setSpacing(6);
    auto *barTitle = new QLabel(QStringLiteral("数据源"), srcBar);
    barTitle->setStyleSheet(QStringLiteral(
        "color:#e6edf3;font-size:13px;font-weight:600;background:transparent;"));
    barLay->addWidget(barTitle);
    m_srcCount = new QLabel(QStringLiteral("3 源 · 2 已连接"), srcBar);
    m_srcCount->setStyleSheet(QStringLiteral("color:#8494a0;font-size:10px;background:transparent;"));
    barLay->addWidget(m_srcCount);
    barLay->addStretch();
    auto mkTiny = [&](const QString &icon, const QString &tip) {
        auto *b = new QToolButton(srcBar);
        b->setIcon(QIcon(QStringLiteral(":/icons/%1.svg").arg(icon)));
        b->setIconSize(QSize(14, 14));
        b->setToolTip(tip);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedSize(22, 22);
        b->setStyleSheet(QStringLiteral(
            "QToolButton{background:transparent;border:0;color:#8494a0;padding:0;}"
            "QToolButton:hover{background:#222b33;}"));
        barLay->addWidget(b);
        return b;
    };
    auto *addSrcBtn = mkTiny(QStringLiteral("newpage"), QStringLiteral("添加数据源"));
    QObject::connect(addSrcBtn, &QToolButton::clicked, this, [this] {
        const QStringList fs = QFileDialog::getOpenFileNames(this, QStringLiteral("添加数据源"), {},
            QStringLiteral("飞行日志 (*.ulg *.bin *.csv *.asc *.blf);;全部 (*)"));
        for (const QString &f : fs)
            loadFile(f);
    });
    auto *runBtn = mkTiny(QStringLiteral("play"), QStringLiteral("全部开始"));
    auto *pauseBtn = mkTiny(QStringLiteral("pause"), QStringLiteral("全部暂停"));
    auto *dockMenuBtn = new QToolButton(srcBar);
    dockMenuBtn->setText(QStringLiteral("▾"));
    dockMenuBtn->setToolTip(QStringLiteral("停靠 / 收起"));
    dockMenuBtn->setCursor(Qt::PointingHandCursor);
    dockMenuBtn->setFixedSize(30, 26);
    dockMenuBtn->setStyleSheet(QStringLiteral(
        "QToolButton{background:#1b232b;border:1px solid #2d3841;border-radius:2px;color:#c9d1d9;"
        "padding:0 0 2px 0;font-size:15px;font-weight:600;}"
        "QToolButton:hover{background:#222b33;border-color:#f2b134;color:#f2b134;}"));
    auto *dockMenu = new QMenu(dockMenuBtn);
    QAction *toLeft = dockMenu->addAction(QStringLiteral("停靠到左侧"));
    QAction *toRight = dockMenu->addAction(QStringLiteral("停靠到右侧"));
    QAction *toBottom = dockMenu->addAction(QStringLiteral("停靠到底部"));
    dockMenu->addSeparator();
    QAction *collapse = dockMenu->addAction(QStringLiteral("自动隐藏（最小化）"));
    QAction *floatAct = dockMenu->addAction(QStringLiteral("浮动 / 取消浮动"));
    dockMenu->addSeparator();
    QAction *closeAct = dockMenu->addAction(QStringLiteral("关闭面板"));
    QObject::connect(toLeft, &QAction::triggered, this, [this, srcDock] { dockToArea(srcDock, this, Qt::LeftDockWidgetArea); });
    QObject::connect(toRight, &QAction::triggered, this, [this, srcDock] { dockToArea(srcDock, this, Qt::RightDockWidgetArea); });
    QObject::connect(toBottom, &QAction::triggered, this, [this, srcDock] { dockToArea(srcDock, this, Qt::BottomDockWidgetArea); });
    QObject::connect(collapse, &QAction::triggered, this, [this, srcDock] { collapseDock(srcDock, this); });
    QObject::connect(floatAct, &QAction::triggered, srcDock, [srcDock] { srcDock->setFloating(!srcDock->isFloating()); });
    QObject::connect(closeAct, &QAction::triggered, srcDock, &QDockWidget::close);
    QObject::connect(dockMenuBtn, &QToolButton::clicked, dockMenuBtn, [dockMenu, dockMenuBtn] {
        dockMenu->exec(dockMenuBtn->mapToGlobal(QPoint(0, dockMenuBtn->height())));
    });
    barLay->addWidget(dockMenuBtn);
    srcDock->setTitleBarWidget(srcBar);

    auto *srcHost = new QWidget(srcDock);
    auto *srcLay = new QVBoxLayout(srcHost);
    srcLay->setContentsMargins(0, 2, 0, 0);
    srcLay->setSpacing(0);

    // 源列表：每条=状态点 + 名称 + 速率/条目 + 配置
    auto *rowsHost = new QWidget(srcHost);
    m_srcRows = new QVBoxLayout(rowsHost);
    m_srcRows->setContentsMargins(0, 0, 0, 0);
    m_srcRows->setSpacing(0);
    addSourceRow(QStringLiteral("ulog · 示例会话"), QStringLiteral("示例 · 未接入"), QStringLiteral("#4a545d"));
    addSourceRow(QStringLiteral("can · vehicle_bus"), QStringLiteral("示例 · 未接入"), QStringLiteral("#4a545d"));
    addSourceRow(QStringLiteral("mav · 192.168.1.20:14550"), QStringLiteral("示例 · 未接入"), QStringLiteral("#4a545d"));
    srcLay->addWidget(rowsHost);

    // 搜索：字段树/参数表/消息表共用，按当前页签生效
    m_srcFilter = new QLineEdit(srcHost);
    m_srcFilter->setPlaceholderText(QStringLiteral("搜索字段 / 参数 / 消息"));
    m_srcFilter->setClearButtonEnabled(true);
    m_srcFilter->setFixedHeight(28);
    m_srcFilter->setStyleSheet(QStringLiteral(
        "QLineEdit{background:#0f151a;border:0;border-top:1px solid #232b33;border-bottom:1px solid #232b33;"
        "color:#c9d1d9;font-size:11px;padding:0 8px;}"
        "QLineEdit:focus{border-bottom:1px solid #f2b134;}"));
    srcLay->addWidget(m_srcFilter);
    connect(m_srcFilter, &QLineEdit::textChanged, this, [this] { applySrcFilter(); });

    // 三页签：字段 / 参数 / 消息
    m_srcTabs = new QTabBar(srcHost);
    m_srcTabs->setExpanding(false);
    m_srcTabs->setDrawBase(false);
    m_srcTabs->addTab(QStringLiteral("字段 0"));
    m_srcTabs->addTab(QStringLiteral("参数 0"));
    m_srcTabs->addTab(QStringLiteral("消息 0"));
    m_srcTabs->setStyleSheet(QStringLiteral(
        "QTabBar{background:#161b20;border-bottom:1px solid #232b33;}"
        "QTabBar::tab{background:transparent;color:#5d6c77;font-size:11px;padding:5px 12px;"
        "border:0;border-bottom:2px solid transparent;}"
        "QTabBar::tab:hover{color:#c8d2da;}"
        "QTabBar::tab:selected{color:#f2b134;border-bottom:2px solid #f2b134;}"));
    srcLay->addWidget(m_srcTabs);
    connect(m_srcTabs, &QTabBar::currentChanged, this, [this] { applySrcFilter(); });

    m_srcStack = new QStackedWidget(srcHost);

    m_srcTree = new ChannelTree(m_srcStack);
    m_srcTree->setHeaderHidden(true);
    m_srcTree->setDragEnabled(true);
    m_srcTree->setDragDropMode(QAbstractItemView::DragOnly);
    m_srcTree->setDefaultDropAction(Qt::CopyAction);
    connect(m_srcTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        if (!item || item->childCount() > 0)
            return;
        const qulonglong ptr = item->data(0, Qt::UserRole).value<qulonglong>();
        if (!ptr)
            return;
        if (auto *c = currentCanvas())
        {
            c->addChannel(reinterpret_cast<const PsChannelInfo *>(ptr));
            log(QStringLiteral("双击加曲线：%1").arg(item->text(0)));
        }
    });
    m_srcTree->setRootIsDecorated(true);
    m_srcTree->setStyleSheet(QStringLiteral("QTreeWidget{border:0;background:#0d1117;font-size:12px;}"));
    m_srcTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_srcTree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        auto *item = m_srcTree->itemAt(pos);
        if (!item || item->childCount() > 0)
            return;
        const QString name = item->data(0, Qt::UserRole + 1).toString();
        if (name.isEmpty())
            return;
        SourceData *src = sourceById(m_activeSource);
        if (!src || !src->ds)
            return;
        QMenu menu(this);
        menu.setAttribute(Qt::WA_StyledBackground, true);
        menu.setAttribute(Qt::WA_TranslucentBackground, false);
        menu.setAutoFillBackground(true);
        QPalette pal = menu.palette();
        pal.setColor(QPalette::Window, QColor(QStringLiteral("#161b20")));
        pal.setColor(QPalette::Base, QColor(QStringLiteral("#161b20")));
        pal.setColor(QPalette::WindowText, QColor(QStringLiteral("#c9d1d9")));
        pal.setColor(QPalette::Text, QColor(QStringLiteral("#c9d1d9")));
        menu.setPalette(pal);
        menu.setStyleSheet(QStringLiteral(
            "QMenu{background:#161b20;color:#c9d1d9;border:1px solid #2d3841;padding:4px;}"
            "QMenu::item{padding:5px 24px 5px 20px;font-size:12px;color:#c9d1d9;background:#161b20;}"
            "QMenu::item:selected{background:#1f2a33;color:#f2b134;}"));
        QAction *add = menu.addAction(QStringLiteral("添加变换…"));
        QAction *draw = menu.addAction(QStringLiteral("画到当前画布"));
        QAction *del = src->derived.contains(name) ? menu.addAction(QStringLiteral("删除派生通道")) : nullptr;
        QAction *chosen = menu.exec(m_srcTree->viewport()->mapToGlobal(pos));
        if (chosen == draw)
        {
            const qulonglong ptr = item->data(0, Qt::UserRole).value<qulonglong>();
            if (auto *c = currentCanvas(); c && ptr)
                c->addChannel(reinterpret_cast<const PsChannelInfo *>(ptr));
            return;
        }
        if (chosen == del && del)
        {
            if (ps_dataset_remove_channel(src->ds, name.toUtf8().constData()) == 0)
            {
                for (int i = src->recipes.size() - 1; i >= 0; --i)
                {
                    const TransformRecipe &r = src->recipes[i];
                    const bool match = r.kind == QLatin1String("quat")
                                           ? name.startsWith(r.output + QLatin1Char('.'))
                                           : r.output == name;
                    if (match)
                        src->recipes.removeAt(i);
                }
                src->derived.removeAll(name);
                for (auto *c : findChildren<CanvasWidget *>())
                    c->removeChannel(name);
                refreshActiveSource();
                log(QStringLiteral("删除派生通道：%1").arg(name));
            }
            return;
        }
        if (chosen != add)
            return;
        addTransformFor(name);
    });
    auto *transformKey = new QShortcut(QKeySequence(QStringLiteral("Ctrl+T")), this);
    connect(transformKey, &QShortcut::activated, this, [this] {
        auto *item = m_srcTree ? m_srcTree->currentItem() : nullptr;
        if (!item || item->childCount() > 0)
            return;
        const QString name = item->data(0, Qt::UserRole + 1).toString();
        if (!name.isEmpty())
            addTransformFor(name);
    });
    m_srcStack->addWidget(m_srcTree);

    m_paramTable = new QTableWidget(0, 2, m_srcStack);
    m_paramTable->setHorizontalHeaderLabels({QStringLiteral("参数"), QStringLiteral("值")});
    m_paramTable->verticalHeader()->setVisible(false);
    m_paramTable->horizontalHeader()->setStretchLastSection(false);
    m_paramTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_paramTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_paramTable->setColumnWidth(1, 110);
    m_paramTable->setShowGrid(false);
    m_paramTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    compactValueTable(m_paramTable);
    m_srcStack->addWidget(m_paramTable);

    m_msgTable = new QTableWidget(0, 3, m_srcStack);
    m_msgTable->setHorizontalHeaderLabels(
        {QStringLiteral("消息"), QStringLiteral("速率"), QStringLiteral("字段")});
    m_msgTable->verticalHeader()->setVisible(false);
    m_msgTable->horizontalHeader()->setStretchLastSection(false);
    m_msgTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_msgTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_msgTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_msgTable->setColumnWidth(1, 62);
    m_msgTable->setColumnWidth(2, 40);
    m_msgTable->setShowGrid(false);
    m_msgTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_msgTable->setSelectionMode(QAbstractItemView::NoSelection);
    compactValueTable(m_msgTable);
    m_srcStack->addWidget(m_msgTable);

    connect(m_srcTabs, &QTabBar::currentChanged, m_srcStack, &QStackedWidget::setCurrentIndex);
    srcLay->addWidget(m_srcStack, 1);

    // 双击通道 → 画到当前页第一格（后续换成字段拖拽，见 §8.10）
    connect(m_srcTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        const qulonglong raw = item->data(0, Qt::UserRole).toULongLong();
        const PsChannelInfo *ch = reinterpret_cast<const PsChannelInfo *>(raw);
        if (!ch)
            return;
        if (auto *c = currentCanvas())
        {
            c->setChannels({ch});
            c->setTitle(QString::fromUtf8(ch->name));
            log(QStringLiteral("画布加载通道：%1").arg(QString::fromUtf8(ch->name)));
        }
    });
    srcDock->setWidget(srcHost);
    addDockWidget(Qt::LeftDockWidgetArea, srcDock);

    connect(runBtn, &QToolButton::clicked, this, [this] {
        m_srcCount->setText(QStringLiteral("3 源 · 运行中"));
        m_srcCount->setStyleSheet(QStringLiteral("color:#7ee787;font-size:10px;background:transparent;"));
        log(QStringLiteral("数据源开始"));
    });
    connect(pauseBtn, &QToolButton::clicked, this, [this] {
        m_srcCount->setText(QStringLiteral("3 源 · 已暂停"));
        m_srcCount->setStyleSheet(QStringLiteral("color:#ffb454;font-size:10px;background:transparent;"));
        log(QStringLiteral("数据源暂停"));
    });

    // 右：属性面板（命中什么对象就显示什么属性）
    auto *propDock = new QDockWidget(QStringLiteral("属性面板"), this);
    propDock->setObjectName(QStringLiteral("dock.prop"));
    auto *propHost = new QWidget(propDock);
    auto *propLay = new QVBoxLayout(propHost);
    propLay->setContentsMargins(0, 0, 0, 0);
    m_selLabel = new QLabel(QStringLiteral("当前对象：画布"), propHost);
    m_selLabel->setContentsMargins(8, 6, 8, 6);
    m_selLabel->setStyleSheet(QStringLiteral("color:#4ea1ff;"));
    m_propTable = new QTableWidget(0, 2, propHost);
    m_propTable->setHorizontalHeaderLabels({QStringLiteral("属性"), QStringLiteral("值")});
    m_propTable->horizontalHeader()->setStretchLastSection(true);
    m_propTable->verticalHeader()->setVisible(false);
    m_propTable->setShowGrid(false);
    compactValueTable(m_propTable);
    propLay->addWidget(m_selLabel);
    propLay->addWidget(m_propTable, 1);
    propDock->setWidget(propHost);
    addDockWidget(Qt::RightDockWidgetArea, propDock);
    setPropRows(QStringLiteral("画布"),
                {{QStringLiteral("提示"), QStringLiteral("选中绘图页签后显示属性")},
                 {QStringLiteral("Y 轴"), QStringLiteral("线性 · 滚轮缩放 / 拖动平移 / 双击复位")}});

    // 右下：脚本
    auto *scriptDock = new QDockWidget(QStringLiteral("脚本 · Lua 5.4"), this);
    scriptDock->setObjectName(QStringLiteral("dock.script"));
    auto *scriptHost = new QWidget(scriptDock);
    auto *scriptLay = new QVBoxLayout(scriptHost);
    scriptLay->setContentsMargins(0, 0, 0, 0);
    m_script = new QPlainTextEdit(scriptHost);
    QFont scriptFont(QStringLiteral("monospace"));
    scriptFont.setStyleHint(QFont::Monospace);
    scriptFont.setPixelSize(12);
    m_script->setFont(scriptFont);
    m_script->setTabStopDistance(28);
    m_script->setPlainText(QStringLiteral(
        "-- 一页一组算子：整块进，向量算，禁止逐点循环\n"
        "rate = ps.derivative(ps.ch(\"roll\"), ps.time())\n"
        "out = ps.movingAvg(rate, 5)\n"
        "ps.plot(\"roll_rate\", out)\n"));
    auto *runScriptBtn = new QPushButton(QStringLiteral("运行"), scriptHost);
    scriptLay->addWidget(m_script, 1);
    scriptLay->addWidget(runScriptBtn);
    scriptDock->setWidget(scriptHost);
    addDockWidget(Qt::RightDockWidgetArea, scriptDock);
    connect(runScriptBtn, &QPushButton::clicked, this, [this, scriptDock] {
        CanvasWidget *canvas = nullptr;
        QVector<ScriptSeries> inputs;
        for (CanvasWidget *c : findChildren<CanvasWidget *>())
        {
            const auto samples = c->sampleSeries();
            if (samples.isEmpty() || (canvas && inputs.size() >= samples.size()))
                continue;
            canvas = c;
            inputs.clear();
            for (const CanvasWidget::SampleSeries &s : samples)
                inputs.append({s.name, s.time, s.value});
        }
        if (!canvas)
        {
            log(QStringLiteral("脚本：当前页没有可见曲线"));
            return;
        }
        {
            QStringList names;
            for (const ScriptSeries &s : inputs)
                names.append(s.name);
            log(QStringLiteral("脚本输入：%1").arg(names.join(QStringLiteral(" | "))));
        }
        const ScriptResult r = ScriptEngine::run(m_script->toPlainText(), inputs);
        if (!r.ok)
        {
            log(QStringLiteral("脚本失败：%1").arg(r.error));
            scriptDock->show();
            scriptDock->raise();
            return;
        }
        for (const ScriptOutput &o : r.outputs)
        {
            canvas->addGeneratedSeries(o.name, o.time, o.value);
            log(QStringLiteral("Lua 输出：%1（%2 点）").arg(o.name).arg(o.value.size()));
        }
        log(QStringLiteral("Lua 5.4 执行完成：%1 条输出").arg(r.outputs.size()));
    });

    // 底部：日志
    auto *logDock = new QDockWidget(QStringLiteral("日志"), this);
    logDock->setObjectName(QStringLiteral("dock.log"));
    m_logView = new QPlainTextEdit(logDock);
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(2000);
    logDock->setWidget(m_logView);
    addDockWidget(Qt::BottomDockWidgetArea, logDock);

    resizeDocks({logDock}, {150}, Qt::Vertical);
}

// dock 标题栏在部分平台/主题下会画成浅灰，QSS 与 palette 都压不住：
// 直接换成自绘的深色标题条（26px，和原型 dock-h 一致），顺带把关闭按钮收进来
void darkenDocks(MainWindow *w)
{
    for (auto *d : w->findChildren<QDockWidget *>())
    {
        if (d->titleBarWidget())
            continue; // 数据源 dock 自带一行式标题栏，别盖一层

        auto *bar = new DockTitleBar(w, d);
        bar->setStyleSheet(QStringLiteral(
            "background:#26303a;border-bottom:1px solid #3a4854;"));
        auto *lay = new QHBoxLayout(bar);
        lay->setContentsMargins(10, 0, 6, 0);
        lay->setSpacing(4);

        auto *title = new QLabel(d->windowTitle(), bar);
        title->setStyleSheet(QStringLiteral(
            "color:#e6edf3;font-size:13px;font-weight:600;background:transparent;"));
        lay->addWidget(title);
        lay->addStretch();

        // 最小化：像 VS 的“自动隐藏”图钉，一键收成侧边竖条
        auto *minBtn = new QToolButton(bar);
        minBtn->setText(QStringLiteral("–"));
        minBtn->setToolTip(QStringLiteral("自动隐藏 / 收起为侧边页签"));
        minBtn->setCursor(Qt::PointingHandCursor);
        minBtn->setFixedSize(30, 26);
        minBtn->setStyleSheet(QStringLiteral(
            "QToolButton{background:transparent;border:0;color:#8494a0;padding:0;font-size:14px;}"
            "QToolButton:hover{color:#4ea1ff;background:#222b33;}"));
        QObject::connect(minBtn, &QToolButton::clicked, w, [w, d] { collapseDock(d, w); });
        lay->addWidget(minBtn);

        // 所有停靠/浮靠/关闭集中到一个菜单，避免标题栏塞满小按钮
        auto *menuBtn = new QToolButton(bar);
        menuBtn->setText(QStringLiteral("⋮"));
        menuBtn->setToolTip(QStringLiteral("停靠 / 自动隐藏 / 浮动 / 关闭"));
        menuBtn->setCursor(Qt::PointingHandCursor);
        menuBtn->setPopupMode(QToolButton::InstantPopup);
        menuBtn->setFixedSize(32, 26);
        menuBtn->setStyleSheet(QStringLiteral(
            "QToolButton{background:#1b232b;border:1px solid #2d3841;border-radius:2px;color:#c9d1d9;"
            "padding:0;font-size:17px;font-weight:700;}"
            "QToolButton:hover{border-color:#f2b134;color:#f2b134;background:#222b33;}"));
        auto *menu = new QMenu(menuBtn);
        QAction *left = menu->addAction(QStringLiteral("停靠到左侧"));
        QAction *right = menu->addAction(QStringLiteral("停靠到右侧"));
        QAction *bottom = menu->addAction(QStringLiteral("停靠到底部"));
        menu->addSeparator();
        QAction *autoHide = menu->addAction(QStringLiteral("自动隐藏（最小化）"));
        QAction *floatAct = menu->addAction(QStringLiteral("浮动 / 取消浮动"));
        menu->addSeparator();
        QAction *close = menu->addAction(QStringLiteral("关闭面板"));
        QObject::connect(left, &QAction::triggered, w, [w, d] { dockToArea(d, w, Qt::LeftDockWidgetArea); });
        QObject::connect(right, &QAction::triggered, w, [w, d] { dockToArea(d, w, Qt::RightDockWidgetArea); });
        QObject::connect(bottom, &QAction::triggered, w, [w, d] { dockToArea(d, w, Qt::BottomDockWidgetArea); });
        QObject::connect(autoHide, &QAction::triggered, w, [w, d] { collapseDock(d, w); });
        QObject::connect(floatAct, &QAction::triggered, d, [d] { d->setFloating(!d->isFloating()); });
        QObject::connect(close, &QAction::triggered, d, &QDockWidget::close);
        menuBtn->setMenu(menu);
        lay->addWidget(menuBtn);
        bar->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(bar, &QWidget::customContextMenuRequested, bar, [bar, menu](const QPoint &pos) {
            menu->exec(bar->mapToGlobal(pos));
        });

        d->setTitleBarWidget(bar);
    }
}

void MainWindow::showSourceMenu(QWidget *row, const QPoint &globalPos)
{
    if (!row)
        return;
    const bool backed = row->property("backed").toBool();
    const QString path = row->property("srcPath").toString();

    QMenu menu(this);
    QAction *toggle = menu.addAction(backed ? QStringLiteral("暂停数据源")
                                            : QStringLiteral("开始数据源"));
    toggle->setEnabled(backed);
    QAction *reload = menu.addAction(QStringLiteral("重新加载"));
    reload->setEnabled(!path.isEmpty());
    QAction *rename = menu.addAction(QStringLiteral("重命名"));

    SourceData *src = sourceById(sourceIdForRow(row));
    QMenu *timeMenu = menu.addMenu(QStringLiteral("时间对齐"));
    timeMenu->setEnabled(backed && src && src->ds);
    QAction *offsetInfo = timeMenu->addAction(
        QStringLiteral("偏移 %1 s").arg(src ? src->offsetSec : 0.0, 0, 'f', 3));
    offsetInfo->setEnabled(false);
    timeMenu->addSeparator();
    QAction *alignStart = timeMenu->addAction(QStringLiteral("对齐起点"));
    QAction *alignCenter = timeMenu->addAction(QStringLiteral("对齐中心"));
    QAction *alignEnd = timeMenu->addAction(QStringLiteral("对齐终点"));
    QAction *base = timeMenu->addAction(QStringLiteral("设为时间基准"));
    timeMenu->addSeparator();
    QAction *resetOffset = timeMenu->addAction(QStringLiteral("重置偏移"));

    QMenu *cmpMenu = menu.addMenu(QStringLiteral("对比同名通道"));
    bool hasOther = false;
    for (SourceData &o : m_sources)
    {
        if (!src || o.id == src->id || !o.ds)
            continue;
        hasOther = true;
        QMenu *sub = cmpMenu->addMenu(o.alias.isEmpty() ? o.name : o.alias);
        QAction *all = sub->addAction(QStringLiteral("叠加全部同名"));
        all->setProperty("cmpAll", true);
        all->setProperty("cmpOther", o.id);
        QAction *one = sub->addAction(QStringLiteral("选择通道…"));
        one->setProperty("cmpAll", false);
        one->setProperty("cmpOther", o.id);
    }
    cmpMenu->setEnabled(hasOther);
    menu.addSeparator();
    QAction *del = menu.addAction(QStringLiteral("移除数据源"));

    QAction *chosen = menu.exec(globalPos);
    if (chosen == toggle)
    {
        QList<QLabel *> labels = row->findChildren<QLabel *>();
        QLabel *meta = labels.value(2, nullptr);
        if (!meta)
            return;
        const QString cur = meta->text();
        if (cur.contains(QStringLiteral("已暂停")))
        {
            meta->setText(row->property("srcMeta").toString());
            log(QStringLiteral("开始数据源：%1").arg(row->property("srcName").toString()));
        }
        else
        {
            meta->setText(QStringLiteral("已暂停 · %1").arg(cur.section(QStringLiteral("·"), 1).trimmed()));
            log(QStringLiteral("暂停数据源：%1").arg(row->property("srcName").toString()));
        }
    }
    else if (chosen == reload)
    {
        log(QStringLiteral("重新加载：%1").arg(path));
        loadFile(path);
    }
    else if (chosen == rename)
    {
        QList<QLabel *> labels = row->findChildren<QLabel *>();
        QLabel *name = labels.value(1, nullptr);
        if (!name)
            return;
        bool ok = false;
        const QString text = QInputDialog::getText(this, QStringLiteral("重命名数据源"),
                                                   QStringLiteral("名称"), QLineEdit::Normal,
                                                   name->text(), &ok);
        if (ok && !text.trimmed().isEmpty())
        {
            name->setText(text.trimmed());
            row->setProperty("srcName", text.trimmed());
            SourceData *src = sourceById(sourceIdForRow(row));
            if (src)
            {
                src->alias = text.trimmed();
                for (auto *c : findChildren<CanvasWidget *>())
                    c->renameSource(src->path, src->alias);
            }
            log(QStringLiteral("重命名数据源：%1").arg(text.trimmed()));
        }
    }
    else if (chosen == alignStart || chosen == alignCenter || chosen == alignEnd)
    {
        alignSource(src, chosen == alignStart ? 0 : (chosen == alignEnd ? 2 : 1));
    }
    else if (chosen == base)
    {
        setSourceBase(src);
    }
    else if (chosen == resetOffset)
    {
        setSourceOffset(src, 0.0, true);
        log(QStringLiteral("重置时间偏移：%1").arg(src ? src->alias : QString()));
    }
    else if (chosen && chosen->property("cmpOther").isValid())
    {
        SourceData *other = sourceById(chosen->property("cmpOther").toInt());
        compareSources(src, other, chosen->property("cmpAll").toBool(), QString());
    }
    else if (chosen == del)
    {
        removeSourceRow(row);
    }
}

void MainWindow::removeSourceRow(QWidget *row)
{
    if (!row || !m_srcRows)
        return;
    const QString name = row->property("srcName").toString();
    const int id = sourceIdForRow(row);
    SourceData *src = sourceById(id);
    if (src)
    {
        const QString path = src->path;
        for (auto *c : findChildren<CanvasWidget *>())
            c->removeChannelsFromSource(path);
        if (src->ds)
            ps_dataset_free(src->ds);
        if (m_activeSource == id)
        {
            m_activeSource = -1;
            m_viewBacked = false;
            m_srcChannels.clear();
            m_srcParams.clear();
            m_srcMsgs.clear();
        }
        int idx = -1;
        for (int i = 0; i < m_sources.size(); ++i)
            if (m_sources.at(i).id == id)
                idx = i;
        if (idx >= 0)
            m_sources.removeAt(idx);
        renderSourceData();
        // 删掉当前源后落到剩下的第一个可绘源
        for (const SourceData &other : m_sources)
        {
            if (other.ds && other.row)
            {
                activateSource(other.row);
                break;
            }
        }
    }
    m_srcRows->removeWidget(row);
    row->deleteLater();
    refreshSrcCount();
    log(QStringLiteral("移除数据源：%1").arg(name));
}

void MainWindow::updateStatusChips()
{
    if (!m_chipSource)
        return;
    int channels = 0;
    for (const SourceData &s : m_sources)
        channels += s.channels.size();
    if (m_sources.isEmpty())
        m_chipSource->setText(QStringLiteral("无数据"));
    else if (m_sources.size() == 1)
        m_chipSource->setText(QStringLiteral("%1 · %2 通道")
                                  .arg(m_sources.first().fmt)
                                  .arg(channels));
    else
        m_chipSource->setText(QStringLiteral("%1 源 · %2 通道")
                                  .arg(m_sources.size())
                                  .arg(channels));
}

void MainWindow::renderSourceData()
{
    updateStatusChips();
    if (!m_srcTree || !m_srcTabs)
        return;
    m_srcTree->clear();
    const int n = m_viewBacked ? m_srcChannels.size() : 0;

    if (!m_viewBacked)
    {
        auto *it = new QTreeWidgetItem(m_srcTree, {QStringLiteral("该数据源暂未提供字段")});
        it->setFlags(Qt::NoItemFlags);
        m_srcTabs->setTabText(0, QStringLiteral("字段 0"));
        m_srcTabs->setTabText(1, QStringLiteral("参数 0"));
        m_srcTabs->setTabText(2, QStringLiteral("消息 0"));

        m_paramTable->setRowCount(1);
        auto *pk = new QTableWidgetItem(QStringLiteral("—"));
        auto *pv = new QTableWidgetItem(QStringLiteral("该数据源未接入，没有参数表"));
        pk->setFlags(Qt::ItemIsEnabled);
        pv->setFlags(Qt::ItemIsEnabled);
        m_paramTable->setItem(0, 0, pk);
        m_paramTable->setItem(0, 1, pv);

        m_msgTable->setRowCount(1);
        for (int c = 0; c < 3; ++c)
        {
            auto *mi = new QTableWidgetItem(c == 0 ? QStringLiteral("—") : QString());
            mi->setFlags(Qt::ItemIsEnabled);
            m_msgTable->setItem(0, c, mi);
        }
        return;
    }

    const bool is_tree = m_srcFmt == QStringLiteral("ULog")
                      || m_srcFmt == QStringLiteral("ArduPilot · BIN")
                      || m_srcFmt.startsWith(QStringLiteral("ASC"))
                      || m_srcFmt.startsWith(QStringLiteral("BLF"));
    QMap<QString, QTreeWidgetItem *> groups;
    SourceData *active = sourceById(m_activeSource);
    QTreeWidgetItem *derivedGroup = nullptr;
    for (const PsChannelInfo *ch : m_srcChannels)
    {
        const QString name = QString::fromUtf8(ch->name);
        QString group;
        QString leaf = name;
        const bool derived = active && active->derived.contains(name);
        if (derived)
        {
            if (!derivedGroup)
            {
                derivedGroup = new QTreeWidgetItem(m_srcTree, {QStringLiteral("派生通道")});
                derivedGroup->setForeground(0, QColor(QStringLiteral("#f2b134")));
            }
            auto *item = new QTreeWidgetItem(derivedGroup, {name.mid(name.lastIndexOf(QLatin1Char('.')) + 1)});
            item->setToolTip(0, name);
            item->setFlags(item->flags() | Qt::ItemIsDragEnabled);
            item->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(reinterpret_cast<qulonglong>(ch)));
            item->setData(0, Qt::UserRole + 1, name);
            continue;
        }
        if (is_tree)
        {
            const int dot = name.lastIndexOf(QLatin1Char('.'));
            if (dot > 0)
            {
                group = name.left(dot);
                leaf = name.mid(dot + 1);
            }
        }
        QTreeWidgetItem *parent = nullptr;
        if (!group.isEmpty())
        {
            auto it = groups.find(group);
            if (it == groups.end())
            {
                auto *g = new QTreeWidgetItem(m_srcTree, {group});
                it = groups.insert(group, g);
            }
            parent = it.value();
        }
        QTreeWidgetItem *item = parent ? new QTreeWidgetItem(parent, {leaf})
                                       : new QTreeWidgetItem(m_srcTree, {leaf});
        item->setToolTip(0, name);
        item->setFlags(item->flags() | Qt::ItemIsDragEnabled);
        item->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(reinterpret_cast<qulonglong>(ch)));
        item->setData(0, Qt::UserRole + 1, name);
    }
    const bool few_groups = groups.size() <= 8;
    for (auto *g : groups)
        g->setExpanded(few_groups && is_tree);
    if (is_tree && !few_groups && !groups.isEmpty())
        groups.first()->setExpanded(true);
    if (derivedGroup)
        derivedGroup->setExpanded(true);

    m_srcTabs->setTabText(0, QStringLiteral("字段 %1").arg(n));
    m_srcTabs->setTabText(1, QStringLiteral("参数 %1").arg(m_srcParams.size()));
    m_srcTabs->setTabText(2, QStringLiteral("消息 %1").arg(m_srcMsgs.size()));

    m_paramTable->setRowCount(m_srcParams.size());
    for (int i = 0; i < m_srcParams.size(); ++i)
    {
        auto *k = new QTableWidgetItem(m_srcParams[i].first);
        auto *v = new QTableWidgetItem(m_srcParams[i].second);
        k->setFlags(Qt::ItemIsEnabled);
        v->setFlags(Qt::ItemIsEnabled);
        k->setForeground(QColor(QStringLiteral("#9aa7b2")));
        v->setForeground(QColor(QStringLiteral("#d5dde5")));
        m_paramTable->setItem(i, 0, k);
        m_paramTable->setItem(i, 1, v);
    }
    if (m_srcParams.isEmpty())
    {
        m_paramTable->setRowCount(1);
        auto *k = new QTableWidgetItem(QStringLiteral("—"));
        auto *v = new QTableWidgetItem(QStringLiteral("%1 未提供参数表（仅 ULog 带 P/Q 参数消息）").arg(m_srcFmt));
        k->setFlags(Qt::ItemIsEnabled);
        v->setFlags(Qt::ItemIsEnabled);
        k->setForeground(QColor(QStringLiteral("#9aa7b2")));
        v->setForeground(QColor(QStringLiteral("#d5dde5")));
        m_paramTable->setItem(0, 0, k);
        m_paramTable->setItem(0, 1, v);
    }

    m_msgTable->setRowCount(m_srcMsgs.size());
    for (int i = 0; i < m_srcMsgs.size(); ++i)
    {
        for (int c = 0; c < 3; ++c)
        {
            auto *mi = new QTableWidgetItem(m_srcMsgs[i].value(c));
            mi->setFlags(Qt::ItemIsEnabled);
            mi->setForeground(QColor(c == 0 ? QStringLiteral("#d5dde5") : QStringLiteral("#9aa7b2")));
            if (c > 0)
                mi->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_msgTable->setItem(i, c, mi);
        }
    }
    if (m_srcMsgs.isEmpty())
    {
        m_msgTable->setRowCount(1);
        for (int c = 0; c < 3; ++c)
        {
            auto *mi = new QTableWidgetItem(QStringLiteral("—"));
            mi->setFlags(Qt::ItemIsEnabled);
            mi->setForeground(QColor(c == 0 ? QStringLiteral("#d5dde5") : QStringLiteral("#9aa7b2")));
            if (c > 0)
                mi->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_msgTable->setItem(0, c, mi);
        }
    }
}

int MainWindow::sourceIdForRow(QWidget *row) const
{
    return row ? row->property("srcId").toInt() : -1;
}

SourceData *MainWindow::sourceById(int id)
{
    for (SourceData &src : m_sources)
        if (src.id == id)
            return &src;
    return nullptr;
}

namespace {
bool sourceTimeRange(const SourceData *s, double &t0, double &t1)
{
    bool init = false;
    for (const PsChannelInfo *ch : s->channels)
    {
        if (!ch || ch->point_count < 2 || !ch->time)
            continue;
        const double a = ch->time[0];
        const double b = ch->time[ch->point_count - 1];
        if (!init) { t0 = a; t1 = b; init = true; }
        else { t0 = std::min(t0, a); t1 = std::max(t1, b); }
    }
    return init;
}
} // namespace

void MainWindow::setSourceOffset(SourceData *src, double offsetSec, bool refit)
{
    if (!src)
        return;
    src->offsetSec = offsetSec;
    for (auto *c : findChildren<CanvasWidget *>())
        c->setSourceOffset(src->path, offsetSec, refit);
    if (auto *row = src->row)
        row->setToolTip(QStringLiteral("%1 · 显示偏移 %2 s").arg(src->alias).arg(offsetSec, 0, 'f', 3));
}

void MainWindow::alignSource(SourceData *src, int mode)
{
    if (!src)
        return;
    double a0 = 0, a1 = 0;
    if (!sourceTimeRange(src, a0, a1))
        return;
    bool init = false;
    double gs = 0, ge = 0;
    for (const SourceData &o : m_sources)
    {
        if (o.id == src->id)
            continue;
        double b0 = 0, b1 = 0;
        if (!sourceTimeRange(&o, b0, b1))
            continue;
        const double ds = b0 - o.offsetSec;
        const double de = b1 - o.offsetSec;
        if (!init) { gs = ds; ge = de; init = true; }
        else { gs = std::min(gs, ds); ge = std::max(ge, de); }
    }
    if (!init)
        return;
    const double target = mode == 0 ? gs : (mode == 2 ? ge : (gs + ge) / 2.0);
    const double center = (a0 + a1) / 2.0;
    const double off = mode == 0 ? a0 - target : (mode == 2 ? a1 - target : center - target);
    setSourceOffset(src, off, true);
    log(QStringLiteral("对齐 %1：%2，偏移 %3 s")
            .arg(src->alias, mode == 0 ? QStringLiteral("起点")
                                       : (mode == 2 ? QStringLiteral("终点") : QStringLiteral("中心")))
            .arg(off, 0, 'f', 3));
}

void MainWindow::setSourceBase(SourceData *src)
{
    double a0 = 0, a1 = 0;
    if (!src || !sourceTimeRange(src, a0, a1))
        return;
    // 基准源的起点为 0，其余源用同一基准换算（PJ globalTimeReference 语义）
    for (SourceData &s : m_sources)
        setSourceOffset(&s, a0, false);
    for (auto *c : findChildren<CanvasWidget *>())
        for (const SourceData &s : m_sources)
            c->setSourceOffset(s.path, a0, true);
    log(QStringLiteral("时间基准：%1，起点归零，其余源同步换算").arg(src->alias));
}

void MainWindow::compareSources(SourceData *src, SourceData *other, bool all, const QString &pick)
{
    if (!src || !other || !currentCanvas())
        return;
    QStringList common;
    for (const PsChannelInfo *a : src->channels)
    {
        if (!a || a->point_count < 2)
            continue;
        const QString n = QString::fromUtf8(a->name);
        for (const PsChannelInfo *b : other->channels)
            if (b && b->point_count > 1 && n == QString::fromUtf8(b->name))
            { common.append(n); break; }
    }
    common.sort();
    if (common.isEmpty())
    {
        log(QStringLiteral("%1 与 %2 没有同名通道").arg(src->alias, other->alias));
        return;
    }
    QStringList names = all ? common : QStringList{pick};
    if (!all)
    {
        bool ok = false;
        const QString n = QInputDialog::getItem(this, QStringLiteral("对比同名通道"),
                                                QStringLiteral("%1 ↔ %2").arg(src->alias, other->alias),
                                                common, qMax(0, common.indexOf(pick)), false, &ok);
        if (!ok || n.isEmpty())
            return;
        names = {n};
    }
    auto *c = currentCanvas();
    int added = 0;
    for (const QString &n : names)
    {
        const PsChannelInfo *a = nullptr;
        const PsChannelInfo *b = nullptr;
        for (const PsChannelInfo *x : src->channels)
            if (x && n == QString::fromUtf8(x->name)) { a = x; break; }
        for (const PsChannelInfo *x : other->channels)
            if (x && n == QString::fromUtf8(x->name)) { b = x; break; }
        if (!a || !b)
            continue;
        c->addChannelEx(a, src->id, src->path, src->alias, src->offsetSec);
        c->addChannelEx(b, other->id, other->path, other->alias, other->offsetSec);
        added += 2;
    }
    log(QStringLiteral("对比 %1 ↔ %2：%3 条曲线").arg(src->alias, other->alias).arg(added));
}

void MainWindow::switchSource(int id)
{
    SourceData *src = sourceById(id);
    if (!src || !src->ds)
        return;
    m_activeSource = id;
    m_viewBacked = true;
    m_srcFmt = src->fmt;
    m_srcChannels = src->channels;
    m_srcParams = src->params;
    m_srcMsgs = src->msgs;
    for (auto *c : findChildren<CanvasWidget *>())
        c->setInputSource(src->id, src->path, src->alias, src->offsetSec);
    renderSourceData();
}

void MainWindow::addTransformFor(const QString &channel)
{
    SourceData *src = sourceById(m_activeSource);
    if (!src || !src->ds)
        return;
    TransformDialog dlg(src->ds, src->channels, src->alias.isEmpty() ? src->name : src->alias, channel, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    for (const QString &n : dlg.addedNames())
        if (!src->derived.contains(n))
            src->derived.append(n);
    TransformRecipe r;
    r.kind = dlg.kind();
    r.input = dlg.inputName();
    r.filter = dlg.filterId();
    r.params = dlg.params();
    r.output = dlg.outputName();
    r.quat[0] = dlg.w();
    r.quat[1] = dlg.x();
    r.quat[2] = dlg.y();
    r.quat[3] = dlg.z();
    r.degrees = dlg.degrees();
    r.unwrap = dlg.unwrap();
    src->recipes.append(r);
    refreshActiveSource();

    QVector<const PsChannelInfo *> out;
    for (const PsChannelInfo *ch : src->channels)
        if (dlg.addedNames().contains(QString::fromUtf8(ch->name)))
            out.append(ch);
    if (out.isEmpty())
        return;
    bool replaced = false;
    if (!dlg.inputName().isEmpty())
        for (auto *c : findChildren<CanvasWidget *>())
            replaced = c->replaceChannel(dlg.inputName(), out.first()) || replaced;
    if (!replaced)
        for (const PsChannelInfo *ch : out)
            if (auto *c = currentCanvas())
                c->addChannel(ch);
    log(QStringLiteral("变换完成：%1").arg(dlg.addedNames().join(QStringLiteral(", "))));
}

void MainWindow::reloadChannels(SourceData *src)
{
    if (!src || !src->ds)
        return;
    src->channels.clear();
    const uint32_t n = ps_dataset_channel_count(src->ds);
    for (uint32_t i = 0; i < n; ++i)
        if (const PsChannelInfo *ch = ps_dataset_channel(src->ds, i))
            src->channels.append(ch);
}

void MainWindow::refreshActiveSource()
{
    SourceData *src = sourceById(m_activeSource);
    if (!src || !src->ds)
        return;
    reloadChannels(src);
    m_srcChannels = src->channels;
    renderSourceData();
}

void MainWindow::activateSource(QWidget *row)
{
    if (!row || !m_srcRows)
        return;
    for (int i = 0; i < m_srcRows->count(); ++i)
    {
        QWidget *w = m_srcRows->itemAt(i) ? m_srcRows->itemAt(i)->widget() : nullptr;
        if (!w)
            continue;
        const bool on = (w == row);
        if (w->property("active").toBool() != on)
        {
            w->setProperty("active", on);
            w->style()->unpolish(w);
            w->style()->polish(w);
        }
    }
    if (row->property("backed").toBool() && sourceById(sourceIdForRow(row)))
    {
        switchSource(sourceIdForRow(row));
        return;
    }
    m_viewBacked = false;
    m_srcChannels.clear();
    m_srcParams.clear();
    m_srcMsgs.clear();
    renderSourceData();
}

QWidget *MainWindow::addSourceRow(const QString &name, const QString &meta, const QString &dotColor, bool backed,
                              const QString &path, int srcId)
{
    if (!m_srcRows)
        return nullptr;
    QWidget *rowsHost = m_srcRows->parentWidget();
    auto *row = new QWidget(rowsHost);
    row->setFixedHeight(40);
    row->setProperty("active", false);
    row->setProperty("connected", dotColor != QStringLiteral("#4a545d"));
    row->setProperty("srcRow", true);
    row->setProperty("backed", backed);
    row->setProperty("srcPath", path);
    row->setProperty("srcMeta", meta);
    row->setProperty("srcName", name);
    row->setProperty("srcId", srcId);
    row->installEventFilter(this);
    row->setStyleSheet(QStringLiteral(
        "QWidget{background:transparent;border-left:2px solid transparent;border-bottom:1px solid #171f26;}"
        "QWidget[active=true]{background:#1a2127;border-left-color:#f2b134;}"
        "QWidget:hover{background:#1d242a;}"));
    auto *lay = new QHBoxLayout(row);
    lay->setContentsMargins(8, 2, 6, 2);
    lay->setSpacing(6);

    auto *dot = new QLabel(row);
    dot->setAttribute(Qt::WA_TransparentForMouseEvents);
    dot->setFixedSize(6, 6);
    dot->setStyleSheet(QStringLiteral("background:%1;border-radius:3px;").arg(dotColor));
    lay->addWidget(dot);

    auto *col = new QWidget(row);
    col->setAttribute(Qt::WA_TransparentForMouseEvents);
    col->setStyleSheet(QStringLiteral("background:transparent;"));
    auto *colLay = new QVBoxLayout(col);
    colLay->setContentsMargins(0, 0, 0, 0);
    colLay->setSpacing(0);
    auto *nameLb = new ElideLabel(name, col);
    nameLb->setStyleSheet(QStringLiteral("color:#c9d1d9;font-size:12px;background:transparent;"));
    auto *metaLb = new QLabel(meta, col);
    metaLb->setStyleSheet(QStringLiteral("color:#5d6c77;font-size:10px;background:transparent;"));
    colLay->addWidget(nameLb);
    colLay->addWidget(metaLb);
    lay->addWidget(col, 1);

    auto *cfg = new QToolButton(row);
    cfg->setIcon(QIcon(QStringLiteral(":/icons/settings.svg")));
    cfg->setIconSize(QSize(14, 14));
    cfg->setToolTip(QStringLiteral("配置数据源"));
    cfg->setCursor(Qt::PointingHandCursor);
    cfg->setFixedSize(22, 22);
    cfg->setStyleSheet(QStringLiteral(
        "QToolButton{background:transparent;border:0;padding:0;}"
        "QToolButton:hover{background:#222b33;border-radius:2px;}"));
    lay->addWidget(cfg);
    connect(cfg, &QToolButton::clicked, this, [this] {
        QMessageBox::information(this, QStringLiteral("配置"),
            QStringLiteral("Loader 附属编辑器（D13）：M1 接字段映射/帧模板。"));
    });

    auto *del = new QToolButton(row);
    del->setIcon(QIcon(QStringLiteral(":/icons/trash.svg")));
    del->setIconSize(QSize(14, 14));
    del->setToolTip(QStringLiteral("移除该数据源"));
    del->setCursor(Qt::PointingHandCursor);
    del->setFixedSize(20, 20);
    del->setStyleSheet(QStringLiteral(
        "QToolButton{background:transparent;border:0;color:#4a545d;padding:0;}"
        "QToolButton:hover{color:#f85149;background:#222b33;}"));
    lay->addWidget(del);
    connect(del, &QToolButton::clicked, this, [this, row] {
        removeSourceRow(row);
    });

    m_srcRows->addWidget(row);
    for (SourceData &src : m_sources)
        if (src.id == srcId)
            src.row = row;
    refreshSrcCount();
    if (backed)
        activateSource(row);
    return row;
}

void MainWindow::refreshSrcCount()
{
    if (!m_srcCount || !m_srcRows)
        return;
    int total = 0;
    int connected = 0;
    for (int i = 0; i < m_srcRows->count(); ++i)
    {
        QWidget *row = m_srcRows->itemAt(i) ? m_srcRows->itemAt(i)->widget() : nullptr;
        if (!row)
            continue;
        ++total;
        if (row->property("connected").toBool())
            ++connected;
    }
    m_srcCount->setText(QStringLiteral("%1 源 · %2 已连接").arg(total).arg(connected));
    m_srcCount->setStyleSheet(QStringLiteral("color:#8494a0;font-size:10px;background:transparent;"));
}

void MainWindow::applySrcFilter()
{
    if (!m_srcFilter || !m_srcTabs)
        return;
    const QString q = m_srcFilter->text().trimmed();
    const int tab = m_srcTabs->currentIndex();
    if (tab == 0 && m_srcTree)
    {
        for (int i = 0; i < m_srcTree->topLevelItemCount(); ++i)
        {
            QTreeWidgetItem *topic = m_srcTree->topLevelItem(i);
            const bool topicHit = q.isEmpty() || topic->text(0).contains(q, Qt::CaseInsensitive);
            int childHit = 0;
            for (int j = 0; j < topic->childCount(); ++j)
            {
                QTreeWidgetItem *leaf = topic->child(j);
                const bool hit = topicHit || leaf->text(0).contains(q, Qt::CaseInsensitive);
                leaf->setHidden(!hit);
                if (hit)
                    ++childHit;
            }
            topic->setHidden(q.isEmpty() ? false : (!topicHit && childHit == 0));
            if (!q.isEmpty() && childHit > 0)
                topic->setExpanded(true);
        }
    }
    else if (tab == 1 && m_paramTable)
    {
        for (int r = 0; r < m_paramTable->rowCount(); ++r)
        {
            bool hit = q.isEmpty();
            for (int c = 0; c < m_paramTable->columnCount() && !hit; ++c)
                hit = m_paramTable->item(r, c) && m_paramTable->item(r, c)->text().contains(q, Qt::CaseInsensitive);
            m_paramTable->setRowHidden(r, !hit);
        }
    }
    else if (tab == 2 && m_msgTable)
    {
        for (int r = 0; r < m_msgTable->rowCount(); ++r)
        {
            bool hit = q.isEmpty();
            for (int c = 0; c < m_msgTable->columnCount() && !hit; ++c)
                hit = m_msgTable->item(r, c) && m_msgTable->item(r, c)->text().contains(q, Qt::CaseInsensitive);
            m_msgTable->setRowHidden(r, !hit);
        }
    }
}

void MainWindow::newPage()
{
    auto *c = new CanvasWidget(this);
    c->clearChannels();
    c->setTitle(QStringLiteral("新页 · 画布"));
    setupCanvas(c);
    const int plusIdx = m_pages->count() - 1;
    m_pages->insertTab(plusIdx, c, QStringLiteral("新页"));
    m_pages->setCurrentWidget(c);
    log(QStringLiteral("新建页"));
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    QTimer::singleShot(0, this, [this] { repositionDockHandles(this); });
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (QSettings(QStringLiteral("NextPilot"), QStringLiteral("PlotScope"))
            .value(QStringLiteral("restoreLayout"), true).toBool())
        saveLayoutTo(defaultLayoutPath());
    QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // 数据源行：点行切换该源自己的字段/参数/消息
    if (event->type() == QEvent::MouseButtonRelease && watched->property("srcRow").toBool())
    {
        auto *me = static_cast<QMouseEvent *>(event);
        QWidget *row = qobject_cast<QWidget *>(watched);
        activateSource(row);
        if (me->button() == Qt::RightButton)
            showSourceMenu(row, me->globalPosition().toPoint());
        return false;
    }
    // Ribbon 按钮的自定义 tooltip：开着它会在鼠标下方盖住分组标题，改走状态栏
    if (event->type() == QEvent::Enter || event->type() == QEvent::Leave)
    {
        if (qobject_cast<SARibbonToolButton *>(watched))
        {
            if (event->type() == QEvent::Enter)
            {
                const QString tip = watched->property("rbTip").toString();
                if (!tip.isEmpty())
                    statusBar()->showMessage(tip);
            }
            else
            {
                statusBar()->showMessage(m_kernelInfo);
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::createPages()
{
    m_pages = new QTabWidget(this);
    m_pages->setDocumentMode(true);
    m_pages->setTabsClosable(true);
    m_pages->tabBar()->setExpanding(false);
    // 只给绘图页签的“＋”开特殊字号，避免 QSS 的 :last 误伤数据源/停靠面板页签
    m_pages->tabBar()->setObjectName(QStringLiteral("pageTabs"));

    auto makeCanvas = [this](const QString &title) {
        auto *c = new CanvasWidget(this);
        c->setTitle(title);
        setupCanvas(c);
        return c;
    };

    // 页 1：三 plot 竖排（QSplitter 树，D15）
    auto *page1 = new QSplitter(Qt::Vertical, this);
    page1->setHandleWidth(6);
    page1->addWidget(makeCanvas(QStringLiteral("姿态角 · plot")));
    page1->addWidget(makeCanvas(QStringLiteral("振动 · plot")));
    page1->addWidget(makeCanvas(QStringLiteral("总线 · plot")));
    page1->setSizes({300, 220, 220});
    m_pages->addTab(page1, QStringLiteral("姿态与振动"));

    // 页 2：单 plot
    m_pages->addTab(makeCanvas(QStringLiteral("车辆总线 · plot")), QStringLiteral("车辆总线"));

    m_pages->addTab(new QLabel(QStringLiteral("页 4 · 仪表大屏：地平仪 / 罗盘 / 表盘（M1）"), this),
                    QStringLiteral("仪表大屏"));

    connect(m_pages, &QTabWidget::tabCloseRequested, this, [this](int idx) {
        if (m_pages->count() > 2 && !m_pages->tabText(idx).isEmpty())
            m_pages->removeTab(idx);
    });

    setCentralWidget(m_pages);
    // 末尾 “+” 页签：浏览器式新建入口，避免挂角落控件被右侧 dock 挤住
    const int plusIdx = m_pages->count();
    m_pages->addTab(new QWidget(m_pages), QStringLiteral("＋"));
    m_pages->tabBar()->setTabToolTip(plusIdx, QStringLiteral("新建页"));
    m_pages->tabBar()->setTabButton(plusIdx, QTabBar::RightSide, nullptr);
    connect(m_pages, &QTabWidget::currentChanged, this, [this](int i) {
        if (i >= 0 && i == m_pages->count() - 1 && m_pages->tabText(i) == QStringLiteral("＋"))
            newPage();
    });

}

void MainWindow::setupCanvas(CanvasWidget *c)
{
    connect(c, &CanvasWidget::activated, this, [this, c](const QString &t) {
        setPropRows(t, {{QStringLiteral("类型"), QStringLiteral("画布")},
                        {QStringLiteral("时间窗"), QStringLiteral("%1 ~ %2 s")
                                                  .arg(c->t0(), 0, 'f', 2)
                                                  .arg(c->t1(), 0, 'f', 2)},
                        {QStringLiteral("数据来源"), c->kernelBacked() ? QStringLiteral("数据集（内核内存）")
                                                                      : QStringLiteral("演示合成数据")},
                        {QStringLiteral("Y 轴"), QStringLiteral("线性 · Ctrl+滚轮缩 / 拖动平移 / 右键复位")}});
    });
    // 同页 X 轴联动：任一画布缩放或平移，其余画布跟着走
    connect(c, &CanvasWidget::timeRangeChanged, this, [this, c](double t0, double t1) {
        QWidget *page = c;
        while (page && page->parentWidget() != m_pages)
            page = page->parentWidget();
        if (!page)
            return;
        QVector<CanvasWidget *> peers;
        collectPageCanvases(page, peers);
        for (CanvasWidget *o : peers)
            if (o != c)
                o->setTimeRange(t0, t1);
    });
    connect(c, &CanvasWidget::addRequested, this, [this] { addCanvasToPage(); });
    connect(c, &CanvasWidget::splitRequested, this, [this, c](int orientation) {
        splitCanvas(c, orientation == 0 ? Qt::Horizontal : Qt::Vertical);
    });
    connect(c, &CanvasWidget::addChannelRequested, this, [this, c] {
        if (m_srcChannels.isEmpty())
        {
            log(QStringLiteral("先加载数据文件，再添加曲线"));
            return;
        }
        QStringList names;
        for (const PsChannelInfo *ch : m_srcChannels)
        {
            const QString n = QString::fromUtf8(ch->name);
            names.append(ch->unit ? QStringLiteral("%1 [%2]").arg(n, QString::fromUtf8(ch->unit)) : n);
        }
        bool ok = false;
        const QString pick = QInputDialog::getItem(this, QStringLiteral("添加曲线"),
                                                   QStringLiteral("通道"), names, 0, false, &ok);
        if (ok && !pick.isEmpty())
        {
            c->addChannel(m_srcChannels.value(names.indexOf(pick)));
            log(QStringLiteral("添加曲线：%1").arg(pick));
        }
    });
    connect(c, &CanvasWidget::removeRequested, this, [this, c] { removeCanvas(c); });
}

void MainWindow::applyCanvasGrid(int rows, int cols)
{
    QWidget *page = m_pages ? m_pages->currentWidget() : nullptr;
    if (!page || dynamic_cast<QLabel *>(page))
    {
        log(QStringLiteral("当前页不是绘图页"));
        return;
    }
    QVector<CanvasWidget *> canvases;
    collectPageCanvases(page, canvases);
    if (canvases.isEmpty())
    {
        log(QStringLiteral("当前页没有画布"));
        return;
    }

    const int idx = m_pages->currentIndex();
    const QString tabText = m_pages->tabText(idx);
    auto *root = buildGridSplitter(canvases, rows, cols, m_pages);
    m_pages->removeTab(idx);
    m_pages->insertTab(idx, root, tabText);
    m_pages->setCurrentIndex(idx);
    page->deleteLater();
    log(QStringLiteral("页内布局：%1 × %2（可继续拆分）").arg(rows).arg(cols));
}

void MainWindow::splitCanvas(CanvasWidget *target, Qt::Orientation orientation)
{
    QWidget *page = m_pages ? m_pages->currentWidget() : nullptr;
    if (!page || !target)
        return;
    if (dynamic_cast<QLabel *>(page))
        return;

    // 网格只是“一次摆好”的快捷方式；拆分前先变成 splitter 树，之后就能任意再分
    if (dynamic_cast<PlotGrid *>(page) || !target->parentWidget() ||
        (target->parentWidget() != page && !qobject_cast<QSplitter *>(target->parentWidget())))
    {
        QVector<CanvasWidget *> canvases;
        collectPageCanvases(page, canvases);
        const int rows = dynamic_cast<PlotGrid *>(page) ? dynamic_cast<PlotGrid *>(page)->rows() : 1;
        const int cols = dynamic_cast<PlotGrid *>(page) ? dynamic_cast<PlotGrid *>(page)->cols() : 1;
        const int idx = m_pages->currentIndex();
        const QString tabText = m_pages->tabText(idx);
        auto *root = buildGridSplitter(canvases, rows, cols, m_pages);
        m_pages->removeTab(idx);
        m_pages->insertTab(idx, root, tabText);
        m_pages->setCurrentIndex(idx);
        page->deleteLater();
        page = root;
    }

    auto *c = new CanvasWidget(m_pages);
    c->clearChannels();
    c->setTitle(QStringLiteral("新画布 · plot"));
    c->setTimeRange(target->t0(), target->t1());
    setupCanvas(c);

    if (target->parentWidget() == page && qobject_cast<CanvasWidget *>(page) == target)
    {
        auto *sp = new QSplitter(orientation, m_pages);
        sp->setHandleWidth(5);
        const int idx = m_pages->currentIndex();
        const QString tabText = m_pages->tabText(idx);
        m_pages->removeTab(idx);
        m_pages->insertTab(idx, sp, tabText);
        m_pages->setCurrentIndex(idx);
        sp->addWidget(target);
        sp->addWidget(c);
        QTimer::singleShot(0, sp, [sp] {
            QList<int> eq;
            for (int i = 0; i < sp->count(); ++i)
                eq << 1;
            sp->setSizes(eq);
        });
        log(QStringLiteral("拆分画布：%1").arg(orientation == Qt::Horizontal ? QStringLiteral("左右")
                                                                              : QStringLiteral("上下")));
        return;
    }

    auto *sp = qobject_cast<QSplitter *>(target->parentWidget());
    if (!sp)
        return;
    const int at = sp->indexOf(target);
    if (sp->orientation() == orientation)
    {
        sp->insertWidget(at + 1, c);
    }
    else
    {
        auto *nested = new QSplitter(orientation, sp);
        nested->setHandleWidth(5);
        sp->insertWidget(at, nested);
        nested->addWidget(target);
        nested->addWidget(c);
        nested->setStretchFactor(0, 1);
        nested->setStretchFactor(1, 1);
        QTimer::singleShot(0, nested, [nested] { nested->setSizes({1, 1}); });
    }
    log(QStringLiteral("拆分画布：%1").arg(orientation == Qt::Horizontal ? QStringLiteral("左右")
                                                                          : QStringLiteral("上下")));
}

void MainWindow::addCanvasToPage()
{
    QWidget *page = m_pages->currentWidget();
    if (qobject_cast<QLabel *>(page))
    {
        log(QStringLiteral("该页不是绘图页，先切到绘图页再加画布"));
        return;
    }

    CanvasWidget *target = currentCanvas();
    if (!target)
        target = pageCanvases().value(0, nullptr);
    if (target)
        splitCanvas(target, Qt::Vertical);
    else
        log(QStringLiteral("当前页没有可拆分的画布"));
}

void MainWindow::removeCanvas(CanvasWidget *c)
{
    if (!c)
        return;
    if (auto *sp = qobject_cast<QSplitter *>(c->parentWidget()))
    {
        if (sp->count() <= 1)
        {
            log(QStringLiteral("页内至少保留一个画布"));
            return;
        }
        c->deleteLater();
        log(QStringLiteral("删除画布"));
        return;
    }
    log(QStringLiteral("页内至少保留一个画布"));
}

QString MainWindow::defaultLayoutPath() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty())
        dir = QDir::homePath() + QStringLiteral("/.plotscope");
    return dir + QStringLiteral("/last.psl");
}

QJsonObject MainWindow::pageState(QWidget *page) const
{
    QJsonObject o;
    if (auto *c = qobject_cast<CanvasWidget *>(page))
    {
        o = c->saveState();
        o[QStringLiteral("type")] = QStringLiteral("canvas");
    }
    else if (auto *g = dynamic_cast<PlotGrid *>(page))
    {
        o[QStringLiteral("type")] = QStringLiteral("grid");
        o[QStringLiteral("rows")] = g->rows();
        o[QStringLiteral("cols")] = g->cols();
        QJsonArray kids;
        for (CanvasWidget *c : g->canvases())
        {
            QJsonObject co = c->saveState();
            co[QStringLiteral("type")] = QStringLiteral("canvas");
            kids.append(co);
        }
        o[QStringLiteral("children")] = kids;
    }
    else if (auto *sp = qobject_cast<QSplitter *>(page))
    {
        o[QStringLiteral("type")] = QStringLiteral("splitter");
        o[QStringLiteral("vertical")] = sp->orientation() == Qt::Vertical;
        QJsonArray sizes;
        for (int v : sp->sizes())
            sizes.append(v);
        o[QStringLiteral("sizes")] = sizes;
        QJsonArray kids;
        for (int i = 0; i < sp->count(); ++i)
            kids.append(pageState(sp->widget(i)));
        o[QStringLiteral("children")] = kids;
    }
    else if (auto *lb = qobject_cast<QLabel *>(page))
    {
        o[QStringLiteral("type")] = QStringLiteral("label");
        o[QStringLiteral("text")] = lb->text();
    }
    return o;
}

QWidget *MainWindow::buildPage(const QJsonObject &o)
{
    const QString type = o.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("canvas"))
    {
        auto *c = new CanvasWidget(m_pages);
        c->clearChannels();
        QHash<QString, const PsChannelInfo *> pool;
        for (const SourceData &src : m_sources)
        {
            for (const PsChannelInfo *ch : src.channels)
            {
                const QString name = QString::fromUtf8(ch->name);
                pool.insert(src.path + QLatin1Char('|') + name, ch);
                if (!pool.contains(name))
                    pool.insert(name, ch);
            }
        }
        c->restoreState(o, pool);
        setupCanvas(c);
        return c;
    }
    if (type == QLatin1String("grid"))
    {
        auto *g = new PlotGrid(m_pages);
        g->setShape(o.value(QStringLiteral("rows")).toInt(1), o.value(QStringLiteral("cols")).toInt(1));
        for (const QJsonValue &v : o.value(QStringLiteral("children")).toArray())
            if (auto *c = qobject_cast<CanvasWidget *>(buildPage(v.toObject())))
                g->addCanvas(c);
        return g;
    }
    if (type == QLatin1String("splitter"))
    {
        auto *sp = new QSplitter(o.value(QStringLiteral("vertical")).toBool(true) ? Qt::Vertical
                                                                                 : Qt::Horizontal,
                                 m_pages);
        sp->setHandleWidth(6);
        for (const QJsonValue &v : o.value(QStringLiteral("children")).toArray())
            sp->addWidget(buildPage(v.toObject()));
        QList<int> sizes;
        for (const QJsonValue &v : o.value(QStringLiteral("sizes")).toArray())
            sizes.append(v.toInt());
        if (!sizes.isEmpty())
            sp->setSizes(sizes);
        return sp;
    }
    return new QLabel(o.value(QStringLiteral("text")).toString(), m_pages);
}

void MainWindow::saveLayoutTo(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QJsonObject root;
    root[QStringLiteral("app")] = QStringLiteral("PlotScope");
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("geometry")] = QString::fromLatin1(saveGeometry().toBase64());
    root[QStringLiteral("docks")] = QString::fromLatin1(saveState().toBase64());
    root[QStringLiteral("current")] = m_pages->currentIndex();

    QJsonArray pages;
    QJsonArray titles;
    const int last = m_pages->count() - 1;
    for (int i = 0; i < m_pages->count(); ++i)
    {
        if (i == last && m_pages->tabText(i) == QStringLiteral("＋"))
            continue;
        pages.append(pageState(m_pages->widget(i)));
        titles.append(m_pages->tabText(i));
    }
    root[QStringLiteral("pages")] = pages;
    root[QStringLiteral("titles")] = titles;

    // 数据源与布局一起保存：重启后先重新加载日志，再回填画布通道
    // 数据源与布局一起保存：重启后按顺序重载全部源，再回填画布通道
    QJsonArray sources;
    for (const SourceData &src : m_sources)
    {
        if (src.path.isEmpty())
            continue;
        QJsonObject so;
        so[QStringLiteral("path")] = src.path;
        so[QStringLiteral("name")] = src.name;
        so[QStringLiteral("alias")] = src.alias;
        so[QStringLiteral("offset")] = src.offsetSec;
        sources.append(so);
    }
    root[QStringLiteral("sources")] = sources;

    QJsonArray transforms;
    for (const SourceData &src : m_sources)
    {
        for (const TransformRecipe &r : src.recipes)
        {
            QJsonObject o;
            o[QStringLiteral("source")] = src.path;
            o[QStringLiteral("kind")] = r.kind;
            o[QStringLiteral("input")] = r.input;
            o[QStringLiteral("filter")] = r.filter;
            o[QStringLiteral("params")] = r.params;
            o[QStringLiteral("output")] = r.output;
            o[QStringLiteral("degrees")] = r.degrees;
            o[QStringLiteral("unwrap")] = r.unwrap;
            if (r.kind == QLatin1String("quat"))
            {
                QJsonArray q;
                for (const QString &s : r.quat)
                    q.append(s);
                o[QStringLiteral("quat")] = q;
            }
            transforms.append(o);
        }
    }
    root[QStringLiteral("transforms")] = transforms;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        log(QStringLiteral("保存布局失败：%1").arg(f.errorString()));
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    QSettings(QStringLiteral("NextPilot"), QStringLiteral("PlotScope")).setValue(QStringLiteral("lastLayout"), path);
    log(QStringLiteral("已保存布局：%1").arg(path));
}

void MainWindow::importLayoutFrom(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        log(QStringLiteral("导入布局失败：%1").arg(f.errorString()));
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    if (root.value(QStringLiteral("app")).toString() != QLatin1String("PlotScope"))
    {
        log(QStringLiteral("不是 PlotScope 布局文件"));
        return;
    }

    // 先恢复数据源，buildPage 才能按通道名取到内核指针
    for (const QJsonValue &v : root.value(QStringLiteral("sources")).toArray())
    {
        const QString path = v.toObject().value(QStringLiteral("path")).toString();
        if (path.isEmpty() || !QFile::exists(path))
            continue;
        bool loaded = false;
        for (int i = 0; m_srcRows && i < m_srcRows->count(); ++i)
        {
            QWidget *row = m_srcRows->itemAt(i) ? m_srcRows->itemAt(i)->widget() : nullptr;
            if (row && row->property("srcPath").toString() == path)
            {
                loaded = true;
                break;
            }
        }
        if (!loaded)
            loadFile(path, false);
        const QString alias = v.toObject().value(QStringLiteral("alias")).toString();
        if (!alias.isEmpty())
        {
            for (SourceData &src : m_sources)
                if (src.path == path)
                    src.alias = alias;
        }
        const double offset = v.toObject().value(QStringLiteral("offset")).toDouble(0.0);
        for (SourceData &src : m_sources)
            if (src.path == path)
                setSourceOffset(&src, offset, false);
    }

    // 重放变换配方；派生通道先写回，buildPage 才能按名字找到
    for (const QJsonValue &v : root.value(QStringLiteral("transforms")).toArray())
    {
        const QJsonObject o = v.toObject();
        SourceData *src = nullptr;
        for (SourceData &s : m_sources)
            if (s.path == o.value(QStringLiteral("source")).toString())
                src = &s;
        if (!src || !src->ds)
            continue;
        TransformRecipe r;
        r.kind = o.value(QStringLiteral("kind")).toString();
        r.input = o.value(QStringLiteral("input")).toString();
        r.filter = o.value(QStringLiteral("filter")).toString();
        r.params = o.value(QStringLiteral("params")).toObject();
        r.output = o.value(QStringLiteral("output")).toString();
        r.degrees = o.value(QStringLiteral("degrees")).toBool(true);
        r.unwrap = o.value(QStringLiteral("unwrap")).toBool(true);
        const QJsonArray q = o.value(QStringLiteral("quat")).toArray();
        for (int i = 0; i < 4 && i < q.size(); ++i)
            r.quat[i] = q.at(i).toString();
        char err[512] = {0};
        int rc = 0;
        if (r.kind == QLatin1String("quat"))
            rc = ps_dataset_apply_quat_rpy(src->ds, r.quat[0].toUtf8().constData(), r.quat[1].toUtf8().constData(),
                                           r.quat[2].toUtf8().constData(), r.quat[3].toUtf8().constData(),
                                           r.output.toUtf8().constData(), r.degrees ? 1 : 0, r.unwrap ? 1 : 0,
                                           err, sizeof err);
        else
            rc = ps_dataset_apply_filter(
                src->ds, r.input.toUtf8().constData(), r.filter.toUtf8().constData(),
                QJsonDocument(r.params).toJson(QJsonDocument::Compact).constData(),
                r.output.toUtf8().constData(), err, sizeof err);
        if (rc != 0)
        {
            log(QStringLiteral("重放变换失败：%1（%2）").arg(r.output, QString::fromUtf8(err)));
            continue;
        }
        src->recipes.append(r);
        const QStringList names = r.kind == QLatin1String("quat")
                                      ? QStringList{r.output + QStringLiteral(".roll"),
                                                    r.output + QStringLiteral(".pitch"),
                                                    r.output + QStringLiteral(".yaw")}
                                      : QStringList{r.output};
        for (const QString &n : names)
            if (!src->derived.contains(n))
                src->derived.append(n);
        reloadChannels(src);
    }
    if (!m_sources.isEmpty())
        refreshActiveSource();

    // 清空页签时屏蔽 currentChanged：末尾的 ＋ 触发 newPage 会和删除循环互踢
    m_pages->blockSignals(true);
    while (m_pages->count() > 0)
        m_pages->removeTab(0);
    m_pages->blockSignals(false);

    const QJsonArray pages = root.value(QStringLiteral("pages")).toArray();
    const QJsonArray titles = root.value(QStringLiteral("titles")).toArray();
    for (int i = 0; i < pages.size(); ++i)
        m_pages->addTab(buildPage(pages[i].toObject()),
                        titles.at(i).toString(QStringLiteral("页 %1").arg(i + 1)));

    const int plusIdx = m_pages->count();
    m_pages->addTab(new QWidget(m_pages), QStringLiteral("＋"));
    m_pages->tabBar()->setTabToolTip(plusIdx, QStringLiteral("新建页"));
    m_pages->tabBar()->setTabButton(plusIdx, QTabBar::RightSide, nullptr);

    const QByteArray geo = QByteArray::fromBase64(root.value(QStringLiteral("geometry")).toString().toLatin1());
    if (!geo.isEmpty())
        restoreGeometry(geo);
    const QByteArray docks = QByteArray::fromBase64(root.value(QStringLiteral("docks")).toString().toLatin1());
    if (!docks.isEmpty())
        restoreState(docks);

    if (m_activeSource >= 0)
        switchSource(m_activeSource);

    const int cur = root.value(QStringLiteral("current")).toInt(0);
    if (cur >= 0 && cur < m_pages->count() - 1)
        m_pages->setCurrentIndex(cur);

    QSettings(QStringLiteral("NextPilot"), QStringLiteral("PlotScope")).setValue(QStringLiteral("lastLayout"), path);
    log(QStringLiteral("已导入布局：%1").arg(path));
}

void MainWindow::openSettings()
{
    QSettings st(QStringLiteral("NextPilot"), QStringLiteral("PlotScope"));
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("设置"));
    auto *lay = new QVBoxLayout(&dlg);
    auto *restore = new QCheckBox(QStringLiteral("启动时恢复上次布局"), &dlg);
    restore->setChecked(st.value(QStringLiteral("restoreLayout"), true).toBool());
    auto *grid = new QCheckBox(QStringLiteral("新建画布默认显示网格"), &dlg);
    grid->setChecked(st.value(QStringLiteral("gridDefault"), true).toBool());
    auto *legend = new QCheckBox(QStringLiteral("新建画布默认显示图例"), &dlg);
    legend->setChecked(st.value(QStringLiteral("legendDefault"), true).toBool());
    lay->addWidget(restore);
    lay->addWidget(grid);
    lay->addWidget(legend);
    auto *clearCache = new QPushButton(QStringLiteral("清空解析缓存"), &dlg);
    lay->addWidget(clearCache);
    connect(clearCache, &QPushButton::clicked, this, [this] {
        DatasetCache::clear();
        log(QStringLiteral("解析缓存已清空"));
    });
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(bb);
    if (dlg.exec() != QDialog::Accepted)
        return;
    st.setValue(QStringLiteral("restoreLayout"), restore->isChecked());
    st.setValue(QStringLiteral("gridDefault"), grid->isChecked());
    st.setValue(QStringLiteral("legendDefault"), legend->isChecked());
    log(QStringLiteral("设置已保存"));
}

QVector<CanvasWidget *> MainWindow::pageCanvases() const
{
    QVector<CanvasWidget *> out;
    if (m_pages)
        collectPageCanvases(m_pages->currentWidget(), out);
    return out;
}

CanvasWidget *MainWindow::currentCanvas() const
{
    return pageCanvases().value(0, nullptr);
}

bool MainWindow::loadFile(const QString &path, bool interactive)
{
    char err[256] = {0};
    PsDataset *ds = nullptr;
    const QFileInfo fi(path);
    // 加载器由 registry 提供：app 不再按扩展名写 if/else，也不再为每个格式写对话框
    const LoaderPlugin *plugin = LoaderRegistry::instance().findBySuffix(fi.suffix());
    if (!plugin || !plugin->load)
    {
        QMessageBox::warning(this, QStringLiteral("加载失败"),
                             QStringLiteral("没有匹配的加载器：%1").arg(fi.fileName()));
        return false;
    }
    const bool is_tree = plugin->tree;
    QString opts;
    if (!plugin->uiDialog.isEmpty())
    {
        // 插件自带 .ui：通用引擎加载，app 不认识任何控件
        if (interactive)
        {
            PluginDialogEngine dlg(*plugin, path, this);
            if (dlg.exec() != QDialog::Accepted)
                return false;
            opts = dlg.optionsJson();
        }
        else if (plugin->defaultOptions)
            opts = plugin->defaultOptions(path);
    }
    else if (!plugin->schema.isEmpty())
    {
        // 交互打开：schema 渲染选项；恢复布局：按加载器默认探测静默加载
        if (interactive)
        {
            LoaderOptionsDialog dlg(*plugin, path, this);
            if (dlg.exec() != QDialog::Accepted)
                return false;
            opts = dlg.optionsJson();
        }
        else if (plugin->defaultOptions)
            opts = plugin->defaultOptions(path);
    }
    // 解析缓存：同文件 + 同 loader + 同选项二次打开直接还原，不再 probe/parse。
    if (plugin->cacheable)
        ds = DatasetCache::load(path, plugin->cacheNamespace, opts);
    if (ds)
    {
        log(QStringLiteral("解析缓存命中：%1").arg(fi.fileName()));
    }
    else
    {
        const int32_t rc = plugin->load(path, opts, &ds, err, sizeof(err));
        if (rc != 0)
        {
            const QString msg = QString::fromUtf8(err);
            log(QStringLiteral("加载失败：%1").arg(msg));
            QMessageBox::warning(this, QStringLiteral("加载失败"), msg);
            return false;
        }
        if (plugin->cacheable)
            DatasetCache::save(path, plugin->cacheNamespace, opts, ds);
    }


    const uint32_t n = ps_dataset_channel_count(ds);
    const QString fmt_tag = plugin->formatTag;
    SourceData src;
    for (const SourceData &old : m_sources)
        src.id = qMax(src.id, old.id + 1);
    src.name = fi.fileName();
    src.alias = fi.completeBaseName();
    for (const SourceData &old : m_sources)
        if (old.alias == src.alias)
            src.alias += QStringLiteral(" #%1").arg(src.id + 1);
    src.path = fi.absoluteFilePath();
    src.fmt = fmt_tag;
    src.ds = ds;
    for (uint32_t i = 0; i < n; ++i)
        if (const PsChannelInfo *ch = ps_dataset_channel(ds, i))
            src.channels.append(ch);

    // 参数：由插件提供 JSON，app 不认格式；可缓存时与数据集同 key 落盘。
    if (plugin->hasParams && plugin->paramsJson)
    {
        QString json;
        if (plugin->cacheable)
            json = DatasetCache::loadParams(src.path, plugin->cacheNamespace, opts);
        const bool fromCache = !json.isEmpty();
        if (!fromCache)
            json = plugin->paramsJson(src.path);
        if (json.isEmpty())
        {
            log(QStringLiteral("参数表读取失败：%1").arg(plugin->id));
        }
        else
        {
            if (!fromCache && plugin->cacheable)
                DatasetCache::saveParams(src.path, plugin->cacheNamespace, opts, json);
            for (const QJsonValue &v : QJsonDocument::fromJson(json.toUtf8()).array())
            {
                const QJsonObject o = v.toObject();
                src.params.append({o.value(QStringLiteral("name")).toString(),
                                    o.value(QStringLiteral("value")).toString()});
            }
            if (fromCache)
                log(QStringLiteral("参数缓存命中：%1").arg(fi.fileName()));
        }
    }

    // 消息：按 topic 聚合（ULog 才有 `topic.field` 层级）
    if (is_tree)
    {
        QMap<QString, QVector<const PsChannelInfo *>> byTopic;
        for (const PsChannelInfo *ch : src.channels)
        {
            const QString name = QString::fromUtf8(ch->name);
            const int dot = name.lastIndexOf(QLatin1Char('.'));
            if (dot > 0)
                byTopic[name.left(dot)].append(ch);
        }
        for (auto it = byTopic.constBegin(); it != byTopic.constEnd(); ++it)
        {
            QString hz = QStringLiteral("—");
            const PsChannelInfo *ch = it.value().isEmpty() ? nullptr : it.value().first();
            if (ch && ch->point_count > 1)
            {
                const double span = ch->time[ch->point_count - 1] - ch->time[0];
                if (span > 0)
                    hz = QStringLiteral("%1 Hz").arg((ch->point_count - 1) / span, 0, 'f',
                                                     (ch->point_count - 1) / span >= 10 ? 0 : 1);
            }
            src.msgs.append({it.key(), hz, QString::number(it.value().size())});
        }
    }

    m_sources.append(src);
    addSourceRow(src.name, QStringLiteral("已加载 · %1 通道").arg(n), QStringLiteral("#7ee787"), true, src.path,
                 src.id);

    // 一格里一个通道：混量纲（deg / rpm / V）塞一条 Y 轴会把小信号压成直线。
    // 自动挑前几路"有变化"的通道：DataFlash 按字母序首段常是 RC 输入全 0。
    const QVector<CanvasWidget *> canvases = pageCanvases();
    const bool firstSource = m_sources.size() == 1;
    if (firstSource)
    {
        QVector<const PsChannelInfo *> pick;
        for (const PsChannelInfo *ch : m_srcChannels)
        {
            const uint32_t np = ch->point_count;
            if (np < 8)
                continue;
            double lo = ch->value[0], hi = lo;
            for (uint32_t k = 1; k < np; ++k)
            {
                lo = qMin(lo, ch->value[k]);
                hi = qMax(hi, ch->value[k]);
            }
            if (hi - lo > 1e-9)
                pick.append(ch);
        }
        if (pick.isEmpty())
            pick = m_srcChannels;
        for (int i = 0; i < canvases.size() && i < pick.size(); ++i)
        {
            canvases[i]->setInputSource(src.id, src.path, src.alias, src.offsetSec);
            canvases[i]->setChannels({pick[i]});
            canvases[i]->setTitle(QStringLiteral("%1 · %2")
                                  .arg(fi.completeBaseName(), QString::fromUtf8(pick[i]->name)));
        }
    }

    if (const PsChannelInfo *first = ps_dataset_channel(ds, 0))
    {
        const double t0 = first->time[0];
        const double t1 = first->time[first->point_count - 1];
        log(QStringLiteral("已加载 %1（%2）：%3 通道 × %4 点，时间窗 %5 s")
                .arg(fi.fileName(), fmt_tag).arg(n).arg(first->point_count)
                .arg(t1 - t0, 0, 'f', 2));
    }

    statusBar()->showMessage(QStringLiteral("%1 · %2 通道 · %3 点").arg(fi.fileName()).arg(n)
                             .arg(ps_dataset_channel(ds, 0) ? ps_dataset_channel(ds, 0)->point_count : 0));
    QSettings recentStore(QStringLiteral("NextPilot"), QStringLiteral("PlotScope"));
    QStringList recent = recentStore.value(QStringLiteral("recentFiles")).toStringList();
    recent.removeAll(fi.absoluteFilePath());
    recent.prepend(fi.absoluteFilePath());
    while (recent.size() > 12)
        recent.removeLast();
    recentStore.setValue(QStringLiteral("recentFiles"), recent);
    return true;
}

void MainWindow::exportActiveCsv()
{
    SourceData *src = sourceById(m_activeSource);
    if (!src || !src->ds)
    {
        log(QStringLiteral("没有可导出的数据源"));
        return;
    }
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("导出数据快照"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
            .filePath(src->alias + QStringLiteral("-snapshot.csv")),
        QStringLiteral("CSV (*.csv)"));
    if (f.isEmpty())
        return;
    QFile out(f);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, QStringLiteral("导出失败"), out.errorString());
        return;
    }
    QTextStream ts(&out);
    ts.setEncoding(QStringConverter::Utf8);
    auto esc = [](const QString &v) {
        QString s = v;
        s.replace(QLatin1Char('"'), QStringLiteral("\""));
        return s.contains(QLatin1Char(',')) || s.contains(QLatin1Char('"')) || s.contains(QLatin1Char('\n'))
            ? QStringLiteral("\"%1\"").arg(s) : s;
    };
    ts << "time,channel,value,unit\n";
    qulonglong rows = 0;
    for (const PsChannelInfo *ch : src->channels)
    {
        if (!ch)
            continue;
        const QString name = esc(QString::fromUtf8(ch->name));
        const QString unit = esc(QString::fromUtf8(ch->unit ? ch->unit : ""));
        for (uint32_t i = 0; i < ch->point_count; ++i)
        {
            ts << QString::number(ch->time[i], 'g', 17) << ',' << name << ','
               << QString::number(ch->value[i], 'g', 17) << ',' << unit << '\n';
            ++rows;
        }
    }
    out.close();
    log(QStringLiteral("已导出数据快照：%1（%2 行）").arg(f).arg(rows));
}

void MainWindow::setPropRows(const QString &object, const QVector<QPair<QString, QString>> &rows)
{
    m_selLabel->setText(QStringLiteral("当前对象：%1").arg(object));
    m_propTable->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i)
    {
        auto *k = new QTableWidgetItem(rows[i].first);
        k->setFlags(Qt::ItemIsEnabled);
        k->setForeground(QColor(QStringLiteral("#9aa7b2")));
        auto *v = new QTableWidgetItem(rows[i].second);
        v->setForeground(QColor(QStringLiteral("#d5dde5")));
        m_propTable->setItem(i, 0, k);
        m_propTable->setItem(i, 1, v);
    }
}

void MainWindow::log(const QString &text)
{
    if (!m_logView)
        return;
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    m_logView->appendPlainText(QStringLiteral("[%1] %2").arg(ts, text));
    statusBar()->showMessage(text, 4000);
}
