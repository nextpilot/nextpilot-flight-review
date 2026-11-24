#include "GridPickPopup.h"

#include "ThemeManager.h"

#include <QApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QVBoxLayout>

// 整个文件包在命名空间里：用 `using namespace` 再定义成员/自由函数，
// 符号会落到全局作用域（编译器不拦，链接器找不到）——这是刚才 undefined reference 的根因。
namespace nextpilot::plotscope::gui
{

namespace
{
QColor token(const char *name)
{
    return ThemeManager::themeColor(QString::fromLatin1(name));
}
} // namespace


GridPickGrid::GridPickGrid(QWidget *parent) : QWidget(parent)
{
    // setMouseTracking 是这套交互的命门：不按下鼠标也要收 move 事件，才能"划过即预览"。
    setMouseTracking(true);
    setFixedSize(kMax * kStep + 2 * kPad, kMax * kStep + 2 * kPad);
    setCursor(Qt::PointingHandCursor);
}

QRect GridPickGrid::cellRect(int r, int c) const
{
    // r/c 从 1 开始（与"行×列"的自然计数一致），左上角是 (1,1)。
    return QRect(kPad + (c - 1) * kStep, kPad + (r - 1) * kStep, kStep - 3, kStep - 3);
}

void GridPickGrid::setSpec(int rows, int cols)
{
    m_curRows = qBound(1, rows, kMax);
    m_curCols = qBound(1, cols, kMax);
    update();
}

void GridPickGrid::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const QColor base = token("base");
    const QColor border = token("border");
    const QColor accent = token("accent");
    const QColor dim = token("textDim");

    for (int r = 1; r <= kMax; ++r)
    {
        for (int c = 1; c <= kMax; ++c)
        {
            const QRect cell = cellRect(r, c);
            const bool inHover = (m_hoverRows >= r && m_hoverCols >= c);
            p.fillRect(cell, inHover ? accent : base);
            p.setPen(inHover ? accent.darker(130) : border);
            p.drawRect(cell.adjusted(0, 0, -1, -1));
        }
    }

    // 当前规格：只在"没在选"的时候画（hover 时实色块已经说明选哪一块，
    // 两个框叠在一起反而眼花——main 干脆什么都不画，等于把当前状态藏起来了）。
    if (m_hoverRows == 0 && m_curRows > 0 && m_curCols > 0)
    {
        const QRect cur = cellRect(1, 1).united(cellRect(m_curRows, m_curCols));
        p.setPen(QPen(dim, 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawRect(cur.adjusted(0, 0, 1, 1));
    }
}

void GridPickGrid::mouseMoveEvent(QMouseEvent *e)
{
    // 反算格子：先减留白再按步距取整；越界时 clamp 到 8，光标贴着边缘也算"选到 8×8"。
    const int c = qBound(1, (e->pos().x() - kPad) / kStep + 1, kMax);
    const int r = qBound(1, (e->pos().y() - kPad) / kStep + 1, kMax);
    if (r != m_hoverRows || c != m_hoverCols)
    {
        m_hoverRows = r;
        m_hoverCols = c;
        update();
        emit hovered(r, c);
    }
}

void GridPickGrid::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || m_hoverRows < 1 || m_hoverCols < 1)
        return;
    emit picked(m_hoverRows, m_hoverCols);
}

void GridPickGrid::leaveEvent(QEvent *)
{
    // 离开就清空 hover：否则上次停留的块会"粘"在格子上，让人以为已经选了。
    m_hoverRows = m_hoverCols = 0;
    update();
}

GridPickPopup::GridPickPopup(QWidget *parent) : QFrame(parent, Qt::Popup)
{
    setObjectName(QStringLiteral("gridPickPopup"));
    setFrameShape(QFrame::NoFrame); // 边框自绘（用主题 token），不靠 QSS

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 8, 8, 6);
    lay->setSpacing(6);

    m_grid = new GridPickGrid(this);
    lay->addWidget(m_grid, 0, Qt::AlignCenter);

    m_hint = new QLabel(QStringLiteral("点击选择　·　Esc 取消"), this);
    m_hint->setAlignment(Qt::AlignCenter);
    m_hint->setObjectName(QStringLiteral("gridPickHint"));
    // 次要信息色：提示不该和格子抢视觉焦点（主题切换时 palette 也会跟着变）。
    QPalette pal = m_hint->palette();
    pal.setColor(QPalette::WindowText, token("textDim"));
    m_hint->setPalette(pal);
    lay->addWidget(m_hint);

    connect(m_grid, &GridPickGrid::hovered, this, [this](int r, int c) {
        syncHint(r, c);
    });
    connect(m_grid, &GridPickGrid::picked, this, [this](int r, int c) {
        m_picked = true;
        m_rows = r;
        m_cols = c;
        hide(); // hideEvent 里统一发 finished
    });
}

void GridPickPopup::setSpec(int rows, int cols)
{
    m_grid->setSpec(rows, cols);
    syncHint(rows, cols);
}

void GridPickPopup::syncHint(int rows, int cols)
{
    // Esc 提示常驻：main 的弹层没有任何文案，第一次用只能靠猜（点外面才知道能取消）。
    if (rows > 0 && cols > 0)
        m_hint->setText(QStringLiteral("%1 × %2　·　Esc 取消").arg(rows).arg(cols));
    else
        m_hint->setText(QStringLiteral("点击选择　·　Esc 取消"));
}

void GridPickPopup::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(r, 4, 4);
    p.fillPath(path, token("panel"));
    p.setPen(QPen(token("border"), 1));
    p.drawPath(path);
}

void GridPickPopup::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape)
    {
        close();
        return;
    }
    QFrame::keyPressEvent(e);
}

void GridPickPopup::hideEvent(QHideEvent *e)
{
    QFrame::hideEvent(e);
    if (!m_emitted)
    {
        m_emitted = true;
        emit finished();
    }
}

bool execGridPick(QWidget *anchor, const QPoint &globalAnchor, int curRows, int curCols,
                  int *outRows, int *outCols)
{
    QWidget *owner = anchor ? anchor->window() : QApplication::activeWindow();
    GridPickPopup popup(owner);
    popup.setSpec(curRows, curCols);
    popup.adjustSize();

    // 定位：贴在鼠标（也就是「网格」按钮）下方 6px，水平居中对齐；
    // 超出屏幕可用区就夹回来，下方放不下就翻到上方 —— 别让弹层跑到屏幕外。
    QScreen *screen = QGuiApplication::screenAt(globalAnchor);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);

    int x = globalAnchor.x() - popup.width() / 2;
    int y = globalAnchor.y() + 6;
    x = qBound(avail.left() + 8, x, qMax(avail.left() + 8, avail.right() - popup.width() - 8));
    if (y + popup.height() > avail.bottom())
        y = globalAnchor.y() - popup.height() - 6;

    popup.move(x, y);
    popup.show();
    popup.setFocus();

    // 局部事件循环：等用户选完或取消。这是"同步语义 + 异步交互"的折中——
    // 上层（MainWindow 的动作派发、脚本 figure.grid.pick）仍拿得到同步返回值，
    // 不用改回写链路（按钮缩略图与"r×c"文字）。
    QEventLoop loop;
    QObject::connect(&popup, &GridPickPopup::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (!popup.picked())
        return false;
    if (outRows)
        *outRows = popup.pickedRows();
    if (outCols)
        *outCols = popup.pickedCols();
    return true;
}

} // namespace nextpilot::plotscope::gui
