#include "Interaction.h"

#include <fastplot/Axes.h>
#include <fastplot/Picker.h>

#include "AxesView.h"
#include "ChannelMime.h"
#include "ContextMenuDecor.h"
#include "CursorReadout.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDrag>
#include <QFileDialog>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

using namespace nextpilot::plotscope::dataset;

namespace nextpilot::plotscope::gui
{

Interaction::Interaction(AxesView *axes, QObject *parent)
    : QObject(parent), m_axes(axes)
{
    m_plot = axes->m_plot;
    m_seriesStore = axes->m_seriesStore;
    m_readout = axes->m_readout;
    m_viewState = axes->m_viewState;
}

void Interaction::attach()
{
    if (!m_plot)
        return;
    m_plot->installEventFilter(this);
    m_plot->setFocusPolicy(Qt::StrongFocus); // 键盘快捷键（P/Esc/Del/[]）需要焦点
}

QVector<CursorReadout::Mark> Interaction::marksForReadout() const
{
    QVector<CursorReadout::Mark> marks;
    marks.reserve(m_bookmarks.size());
    for (const AxesView::Bookmark &bm : m_bookmarks)
        marks.append({bm.id, bm.t, bm.label, bm.id == m_selectedBookmarkId});
    return marks;
}

bool Interaction::handlePlotEvent(QObject *watched, QEvent *event)
{
    if (watched != m_plot || !m_plot)
        return m_axes->QObject::eventFilter(watched, event);

    switch (event->type())
    {
    case QEvent::MouseMove:
        onMouseMove(static_cast<QMouseEvent *>(event));
        break;
    case QEvent::MouseButtonDblClick:
    {
        // 双击标题栏 = 行内改名：
        // 只认标题带那一行，画图区双击仍然给 Zoomer/Picker。
        const QMouseEvent *me = static_cast<QMouseEvent *>(event);
        if (me->position().y() <= fastplot::Axes::kTitleBarHeight + 2)
        {
            bool ok = false;
            const QString name = QInputDialog::getText(
                m_axes, QStringLiteral("重命名画布"), QStringLiteral("标题"),
                QLineEdit::Normal, m_axes->canvasTitle(), &ok);
            if (ok)
            {
                m_axes->setCanvasTitle(name);
                return true;   // 改名这一下不再落到双击缩放
            }
        }
        break;
    }
    case QEvent::MouseButtonPress:
        onMousePress(static_cast<QMouseEvent *>(event));
        break;
    case QEvent::MouseButtonRelease:
        onMouseRelease(static_cast<QMouseEvent *>(event));
        break;
    case QEvent::Wheel:
        onWheel(static_cast<QWheelEvent *>(event));
        break;
    default:
        break;
    }
    // 始终不吞事件：本过滤器只"顺便"维护读数状态，导航仍交给 Panner/Zoomer。
    return m_axes->QObject::eventFilter(watched, event);
}

void Interaction::onMouseMove(QMouseEvent *event)
{
    if (!m_plot)
        return;

    // 图例上按住拖动 = 把这条曲线移到另一画布。
    // 放在 plotRect 判断之前：图例可能画在绘图区边缘，拖出去不该被早退吃掉。
    if (m_legendDragRow >= 0 && (event->buttons() & Qt::LeftButton)
        && (event->pos() - m_pressPos).manhattanLength() > QApplication::startDragDistance()
        && m_legendDragRow < m_seriesStore->all().size())
    {
        const PlotSeries sp = m_seriesStore->all().at(m_legendDragRow);
        QMimeData *mime = new QMimeData;
        mime->setData(QString::fromLatin1(series_mime::type()),
                      series_mime::encode(m_axes->m_canvasId, sp.key(), sp.datasetId, sp.table,
                                          sp.column, sp.title).toUtf8());
        QDrag *drag = new QDrag(m_axes);
        drag->setMimeData(mime);
        m_legendDragRow = -1;   // 交给 QDrag：release 不再当点击处理
        drag->exec(Qt::MoveAction);
        return;
    }

    if (!m_plot->plotRect().contains(event->pos()))
        return;

    // 拖动进行中：更新被拖对象。
    if (m_dragKind != 0)
    {
        const double t = timeAtPixel(event->pos().x());
        if (m_dragKind == 1) m_readout->setABStart(t);
        else if (m_dragKind == 2) m_readout->setABEnd(t);
        else if (m_dragKind == 3)
        {
            for (AxesView::Bookmark &bm : m_bookmarks)
                if (bm.id == m_dragBookmarkId) bm.t = t;
            std::sort(m_bookmarks.begin(), m_bookmarks.end(),
                      [](const AxesView::Bookmark &a, const AxesView::Bookmark &c) { return a.t < c.t; });
        }
        else if (m_dragKind == 4) m_readout->setCursorTime(t);
        if (std::fabs(event->pos().x() - m_pressPos.x()) > QApplication::startDragDistance())
            m_pressMoved = true;
        m_axes->refreshReadout();
        if (m_dragKind == 3) Q_EMIT m_axes->bookmarksChanged();
        if (m_dragKind == 4) Q_EMIT m_axes->linkedTimeChanged(m_readout->cursorTime(), true, m_axes);
        return;
    }

    // 修饰键：Shift 拖 = 移动书签、Ctrl = A/B 编辑态；普通悬停 = 吸附最近样本时间。
    const AxesView::HitResult hit = hitTest(event->pos());
    if ((event->buttons() & Qt::LeftButton) && hit.kind == AxesView::Hit::Bookmark)
    {
        m_dragKind = 3;
        m_dragBookmarkId = hit.bookmarkId;
        m_pressPos = event->pos();
        return;
    }
    if ((event->buttons() & Qt::LeftButton) && (hit.kind == AxesView::Hit::AB_A || hit.kind == AxesView::Hit::AB_B))
    {
        m_dragKind = hit.kind == AxesView::Hit::AB_A ? 1 : 2;
        m_pressPos = event->pos();
        return;
    }

    // 悬停：取最近样本时间，更新读数（不 pinned）。联动只同步时间。
    double sx = 0.0, sy = 0.0;
    if (m_plot->nearestSample(event->pos(), &sx, &sy))
        m_axes->setCursorTime(sx, false);
}

void Interaction::onMousePress(QMouseEvent *event)
{
    if (!m_plot || !m_plot->plotRect().contains(event->pos()))
        return;
    m_pressPos = event->pos();
    m_pressMoved = false;

    if (event->button() != Qt::LeftButton)
        return;
    const Qt::KeyboardModifiers mods = event->modifiers();

    // 图例命中：点击标题栏折叠/展开、点击行切显隐。
    // 图例区优先于修饰键复合（Ctrl+点图例行 = 切显隐，不生成 A/B）。
    {
        const int legendHit = m_plot->legendHitTest(event->pos());
        if (legendHit == -1)
        {
            m_plot->setLegendCollapsed(!m_plot->legendCollapsed());
            return;
        }
        if (legendHit >= 0)
        {
            // 点击切显隐、拖动移动曲线——两者共用图例行，靠"是否移动过"区分。
            m_legendDragRow = legendHit;
            return;
        }
    }

    // Ctrl+左键：直接生成一组 A/B。
    if (mods & Qt::ControlModifier)
    {
        double sx = 0.0, sy = 0.0;
        if (m_plot->nearestSample(event->pos(), &sx, &sy))
            m_readout->setCursorTime(sx);
        m_axes->toggleAB();
        return;
    }
    // Shift+左键：添加时间书签。
    if (mods & Qt::ShiftModifier)
    {
        double sx = 0.0, sy = 0.0;
        m_plot->nearestSample(event->pos(), &sx, &sy);
        m_axes->addBookmark(sx);
        return;
    }

    const AxesView::HitResult hit = hitTest(event->pos());
    if (hit.kind == AxesView::Hit::Bookmark)
    {
        m_selectedBookmarkId = hit.bookmarkId;
        m_dragKind = 3;
        m_dragBookmarkId = hit.bookmarkId;
        m_axes->refreshReadout();
    }
    else if (hit.kind == AxesView::Hit::AB_A || hit.kind == AxesView::Hit::AB_B)
    {
        m_dragKind = hit.kind == AxesView::Hit::AB_A ? 1 : 2;
    }
    else if (hit.kind == AxesView::Hit::CursorLine)
    {
        m_dragKind = 4;
    }
}

void Interaction::onMouseRelease(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_dragKind = 0;
    m_dragBookmarkId.clear();
    // 图例上"按下-松开"没有位移 = 点击切显隐（拖动已由 mouseMove 启动 QDrag 清掉候选）。
    if (m_legendDragRow >= 0)
    {
        if (m_plot && !m_pressMoved)
            m_plot->toggleLegendItem(m_legendDragRow);
        m_legendDragRow = -1;
    }
}

void Interaction::onWheel(QWheelEvent *event)
{
    // Alt+滚轮且悬停在图例区：图例字号步进。
    // Ctrl+滚轮保留给 Y 轴缩放（canvas-interaction.md 既有约定）。
    if ((event->modifiers() & Qt::AltModifier) && m_plot
        && m_plot->legendHitTest(event->position().toPoint()) != -2)
    {
        m_plot->zoomLegendFont(event->angleDelta().y() > 0 ? 1 : -1);
        event->accept();
    }
    // 缩放/平移后，固定读数要跟着时间映射重画；悬停读数由下一次 mouse move 刷新。
    if (m_readout->pinned())
        m_axes->refreshReadout();
}

void Interaction::keyPress(QKeyEvent *event)
{
    const int key = event->key();
    const Qt::KeyboardModifiers mods = event->modifiers();

    // Ctrl+Z：撤销上一次缩放/平移。
    if (key == Qt::Key_Z && (mods & Qt::ControlModifier))
    {
        m_axes->undoView();
        return;
    }
    if (key == Qt::Key_P && !(mods & Qt::ControlModifier))
    {
        // P：固定/解除当前读数。
        if (m_readout->active())
            m_axes->setCursorTime(m_readout->cursorTime(), !m_readout->pinned());
        return;
    }
    if (key == Qt::Key_Escape)
    {
        // Esc 层级退出：拖动 > A/B > 书签选中 > 固定读数。
        if (m_dragKind) { m_dragKind = 0; return; }
        if (m_readout->abEnabled()) { m_axes->clearAB(); return; }
        if (!m_selectedBookmarkId.isEmpty()) { m_selectedBookmarkId.clear(); m_axes->refreshReadout(); return; }
        if (m_readout->pinned()) { m_readout->setPinned(false); m_axes->refreshReadout(); return; }
        m_axes->clearCursor();
        return;
    }
    if (key == Qt::Key_Delete || key == Qt::Key_Backspace)
    {
        if (!m_selectedBookmarkId.isEmpty())
            m_axes->removeBookmark(m_selectedBookmarkId);
        return;
    }
    if (key == Qt::Key_BracketLeft || key == Qt::Key_BracketRight)
    {
        // [ / ]：上/下一个时间书签跳转。
        if (m_bookmarks.isEmpty())
            return;
        const bool forward = key == Qt::Key_BracketRight;
        if (m_selectedBookmarkId.isEmpty())
        {
            m_selectedBookmarkId = forward ? m_bookmarks.first().id : m_bookmarks.last().id;
        }
        else
        {
            int idx = -1;
            for (int i = 0; i < m_bookmarks.size(); ++i)
                if (m_bookmarks.at(i).id == m_selectedBookmarkId) { idx = i; break; }
            idx = forward ? qMin(idx + 1, m_bookmarks.size() - 1) : qMax(idx - 1, 0);
            m_selectedBookmarkId = m_bookmarks.at(idx).id;
        }
        for (const AxesView::Bookmark &bm : m_bookmarks)
            if (bm.id == m_selectedBookmarkId) { m_axes->setCursorTime(bm.t, true); break; }
        m_axes->refreshReadout();
        return;
    }
    m_axes->QWidget::keyPressEvent(event);
}

void Interaction::contextMenu(QContextMenuEvent *event)
{
    // 空画布与有曲线共用同一套菜单。
    // 早先这里 `m_seriesStore->all().isEmpty()` 提前 return，只给 4 项（属性三项 + 清游标）——
    // 于是空画布上想"显示网格 / 导出 / 加书签"全都没有入口（实测踩到）。main 同一位置 13 项。
    QMenu menu(m_axes);
    menu.setTitle(QStringLiteral("画布操作"));

    // 统一选中入口：把画布/轴选中交给上层 -> 属性面板。
    QMenu *inspect = menu.addMenu(QStringLiteral("属性"));
    inspect->addAction(QStringLiteral("画布属性"), this, [this] { Q_EMIT m_axes->canvasSelectionRequested(); });
    inspect->addAction(QStringLiteral("X 轴属性"), this, [this] { Q_EMIT m_axes->axisSelectionRequested(QStringLiteral("x")); });
    inspect->addAction(QStringLiteral("左 Y 轴属性"), this, [this] { Q_EMIT m_axes->axisSelectionRequested(QStringLiteral("y-left")); });
    inspect->addAction(QStringLiteral("右 Y 轴属性"), this, [this] { Q_EMIT m_axes->axisSelectionRequested(QStringLiteral("y-right")); });
    menu.addSeparator();

    // 读数测量（对齐老 PlottingCanvas 右键菜单）。
    const AxesView::HitResult hit = hitTest(event->pos());
    const double clickT = timeAtPixel(event->pos().x());
    if (hit.kind == AxesView::Hit::Bookmark)
    {
        const QString id = hit.bookmarkId;
        QMenu *bm = menu.addMenu(QStringLiteral("时间书签"));
        bm->addAction(QStringLiteral("重命名…"), this, [this, id] {
            bool ok = false;
            QString cur;
            for (const AxesView::Bookmark &b : m_bookmarks) if (b.id == id) cur = b.label;
            const QString text = QInputDialog::getText(m_axes, QStringLiteral("重命名书签"),
                QStringLiteral("标签："), QLineEdit::Normal, cur, &ok);
            if (ok) m_axes->renameBookmark(id, text);
        });
        bm->addAction(QStringLiteral("删除"), this, [this, id] { m_axes->removeBookmark(id); });
    }
    else
    {
        // 文案带点击时刻（main 的"在 16.02 添加时间书签…"语义）：
        // 用户不必先把游标挪过去，右键点在哪一刻就打到哪一刻。
        menu.addAction(QStringLiteral("在 %1 s 添加时间书签").arg(QString::number(clickT, 'f', 2)),
                       this, [this, clickT] { m_axes->addBookmark(clickT); });
    }
    menu.addAction(m_readout->abEnabled() ? QStringLiteral("清除 A/B 测量") : QStringLiteral("A/B 游标测量（Δt/Δy）"),
                   this, [this] { m_axes->toggleAB(); });
    if (m_readout->active() && m_readout->pinned())
        menu.addAction(QStringLiteral("取消固定读数"), this, [this] { m_readout->setPinned(false); m_axes->refreshReadout(); });
    else
        menu.addAction(QStringLiteral("固定当前读数"), this, [this, clickT] {
            if (!m_readout->active()) m_axes->setCursorTime(clickT, true);
            else m_axes->setCursorTime(m_readout->cursorTime(), true);
        });
    menu.addSeparator();

    // 视图操作（右键菜单常用项）。
    menu.addAction(QStringLiteral("放大"), this, [this] { m_axes->zoomRange(0.5); });
    menu.addAction(QStringLiteral("缩小"), this, [this] { m_axes->zoomRange(2.0); });
    menu.addAction(QStringLiteral("自适应"), this, [this] { m_axes->autoScale(); });
    auto *undoAct = menu.addAction(QStringLiteral("撤销视图 (Ctrl+Z)"), this, [this] { m_axes->undoView(); });
    undoAct->setEnabled(m_axes->canUndoView());
    menu.addSeparator();

    // 「添加曲线」：ng 还没有通道选择对话框，这里给禁用项 + 指路，
    // 而不是放一个点了没反应的假入口（main 那版是弹 ChannelPicker，ng 暂缺）。
    auto *addSeries = menu.addAction(QStringLiteral("添加曲线…"));
    addSeries->setEnabled(false);
    addSeries->setToolTip(QStringLiteral("从左侧数据集面板拖入字段，或在字段右键选「画到当前画布」"));
    menu.addSeparator();
    // 显示网格 / 显示图例：主题里 QMenu::indicator 不绘制，单靠 setCheckable
    // 用户看不出当前开关态，所以文案前置 ✓（与 main 同一位置、同一写法）。
    auto *gridAction = menu.addAction(QString());
    auto syncGridLabel = [gridAction](bool on) {
        gridAction->setText(on ? QStringLiteral("✓ 显示网格") : QStringLiteral("显示网格"));
    };
    gridAction->setCheckable(true);
    gridAction->setChecked(m_plot && m_plot->gridVisible());
    syncGridLabel(gridAction->isChecked());
    connect(gridAction, &QAction::triggered, this, [this, syncGridLabel](bool on) {
        if (m_plot) { m_plot->grid(on); }
        syncGridLabel(on);
    });
    auto *legendAction = menu.addAction(QString());
    auto syncLegendLabel = [legendAction](bool on) {
        legendAction->setText(on ? QStringLiteral("✓ 显示图例") : QStringLiteral("显示图例"));
    };
    legendAction->setCheckable(true);
    legendAction->setChecked(m_axes->legendVisible());
    syncLegendLabel(legendAction->isChecked());
    connect(legendAction, &QAction::triggered, this, [this, syncLegendLabel](bool on) {
        m_axes->setLegendVisible(on);
        syncLegendLabel(on);
    });
    menu.addSeparator();

    // 曲线子菜单：每条曲线 = 显隐 + 挂左/右轴 + 删除（老 showSeriesMenu 的体验）。
    QMenu *seriesMenu = menu.addMenu(QStringLiteral("曲线"));
    for (const PlotSeries &s : m_seriesStore->all())
    {
        const QString key = s.key();
        QMenu *one = seriesMenu->addMenu(s.legendName());
        auto *vis = one->addAction(QStringLiteral("显示"));
        vis->setCheckable(true);
        vis->setChecked(s.visible);
        connect(vis, &QAction::triggered, this, [this, key](bool on) { m_axes->setSeriesVisible(key, on); });
        auto *right = one->addAction(QStringLiteral("挂右侧 Y 轴"));
        right->setCheckable(true);
        right->setChecked(s.rightAxis);
        connect(right, &QAction::triggered, this, [this, key](bool on) { m_axes->setSeriesAxis(key, on); });
        one->addSeparator();
        one->addAction(QStringLiteral("属性"), this, [this, key] { Q_EMIT m_axes->seriesSelectionRequested(key); });
        one->addAction(QStringLiteral("Y 轴变换…"), this, [this, key] { Q_EMIT m_axes->seriesTransformRequested(key); });
        one->addAction(QStringLiteral("删除"), this, [this, key] { m_axes->removeSeries(key); });
    }
    menu.addSeparator();

    // 画布级操作（页内）：本画布只发信号，由 PlotWorkspace 反查索引后执行——
    // 保证"删的是你右键的那个画布"，而不是碰巧处于激活态的另一个。
    menu.addAction(QStringLiteral("向右拆分"), this,
                   [this] { Q_EMIT m_axes->canvasActionRequested(QStringLiteral("splitHorizontal")); });
    menu.addAction(QStringLiteral("向下拆分"), this,
                   [this] { Q_EMIT m_axes->canvasActionRequested(QStringLiteral("splitVertical")); });

    // 导出本画布：自己开文件对话框（main 的"导出当前画布"在同一位置），
    // 不绕回 MainWindow 的 action 路径，避免"导出的是当前激活画布而不是右键画布"。
    menu.addAction(QStringLiteral("导出 PNG…"), this, [this] {
        const QString path = QFileDialog::getSaveFileName(m_axes, QStringLiteral("导出 PNG"), QString(),
                                                          QStringLiteral("PNG 图片 (*.png)"));
        if (!path.isEmpty())
            m_axes->exportPng(path);
    });
    menu.addAction(QStringLiteral("导出当前画布 CSV…"), this, [this] {
        const QString path = QFileDialog::getSaveFileName(m_axes, QStringLiteral("导出 CSV"), QString(),
                                                          QStringLiteral("CSV 表格 (*.csv)"));
        if (!path.isEmpty())
            m_axes->exportCsv(path);
    });

    menu.addSeparator();
    // 单画布页删不到 0：置灰（由 workspace 重排时同步），而不是点了才弹提示。
    auto *removeAct = menu.addAction(QStringLiteral("删除此画布"), this,
                                     [this] { Q_EMIT m_axes->canvasActionRequested(QStringLiteral("removeCanvas")); });
    removeAct->setEnabled(m_axes->m_canvasRemovable);
    if (!m_axes->m_canvasRemovable)
        removeAct->setToolTip(QStringLiteral("页内至少保留一个画布"));

    // 图标集中装饰（见文件头 decorateMenu）：文案是本地构造的，改文案要同步映射表。
    decorateMenu(&menu);
    menu.exec(event->globalPos());
    event->accept();
}

AxesView::HitResult Interaction::hitTest(const QPointF &pixel) const
{
    AxesView::HitResult res;
    if (!m_plot)
        return res;
    const QRectF plot = m_plot->plotRect();
    if (!plot.contains(pixel))
        return res;
    constexpr double kTolPx = 10.0;

    // 书签 chip 命中：竖线附近优先（选中/拖动）。
    for (const AxesView::Bookmark &bm : m_bookmarks)
    {
        const double x = m_plot->xToPixel(bm.t);
        if (std::fabs(x - pixel.x()) <= kTolPx)
        {
            res.kind = AxesView::Hit::Bookmark;
            res.bookmarkId = bm.id;
            return res;
        }
    }
    if (m_readout->abEnabled())
    {
        if (std::fabs(m_plot->xToPixel(m_readout->abA()) - pixel.x()) <= kTolPx)
        {
            res.kind = AxesView::Hit::AB_A;
            return res;
        }
        if (m_readout->abHasB() && std::fabs(m_plot->xToPixel(m_readout->abB()) - pixel.x()) <= kTolPx)
        {
            res.kind = AxesView::Hit::AB_B;
            return res;
        }
    }
    if (m_readout->active() && std::fabs(m_plot->xToPixel(m_readout->cursorTime()) - pixel.x()) <= kTolPx)
    {
        res.kind = AxesView::Hit::CursorLine;
        return res;
    }
    return res;
}

double Interaction::timeAtPixel(double px) const
{
    return m_plot ? m_plot->pixelToX(px) : 0.0;
}

void Interaction::addBookmark(double t, const QString &label)
{
    // 同一时间点已有书签则选中它，不重复创建。
    for (const AxesView::Bookmark &bm : m_bookmarks)
    {
        if (std::fabs(bm.t - t) < 1e-9)
        {
            m_selectedBookmarkId = bm.id;
            m_axes->refreshReadout();
            return;
        }
    }
    AxesView::Bookmark bm;
    bm.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    bm.t = t;
    bm.label = label.trimmed().isEmpty() ? QStringLiteral("B%1").arg(++m_bookmarkSeq) : label.trimmed();
    m_bookmarks.append(bm);
    m_selectedBookmarkId = bm.id;
    std::sort(m_bookmarks.begin(), m_bookmarks.end(),
              [](const AxesView::Bookmark &a, const AxesView::Bookmark &c) { return a.t < c.t; });
    m_axes->refreshReadout();
    Q_EMIT m_axes->bookmarksChanged();
}

void Interaction::removeBookmark(const QString &id)
{
    const int before = m_bookmarks.size();
    m_bookmarks.erase(std::remove_if(m_bookmarks.begin(), m_bookmarks.end(),
                                     [&](const AxesView::Bookmark &bm) { return bm.id == id; }),
                      m_bookmarks.end());
    if (m_bookmarks.size() == before)
        return;
    if (m_selectedBookmarkId == id)
        m_selectedBookmarkId.clear();
    m_axes->refreshReadout();
    Q_EMIT m_axes->bookmarksChanged();
}

void Interaction::renameBookmark(const QString &id, const QString &label)
{
    if (label.trimmed().isEmpty())
        return;
    for (AxesView::Bookmark &bm : m_bookmarks)
    {
        if (bm.id == id)
        {
            bm.label = label.trimmed();
            m_axes->refreshReadout();
            Q_EMIT m_axes->bookmarksChanged();
            return;
        }
    }
}
} // namespace nextpilot::plotscope::gui
