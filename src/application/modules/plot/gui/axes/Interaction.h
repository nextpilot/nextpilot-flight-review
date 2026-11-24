#pragma once

#include <QObject>
#include <QPointF>
#include <QString>
#include <QVector>

#include "AxesView.h" // Bookmark / HitResult：交互的产出类型
#include "CursorReadout.h"

namespace nextpilot::plotscope::fastplot
{
class Axes;
}

namespace nextpilot::plotscope::gui
{

class AxesView;
class SeriesStore;
class ViewState;

// 绘图区交互部件：鼠标状态机（拖动游标 / A-B / 书签）、图例拖出、键盘快捷、
// 右键菜单与命中测试。只改状态、发信号，不自己画（画由 CursorReadout / fastplot 负责）。
class Interaction : public QObject
{
    Q_OBJECT

public:
    explicit Interaction(AxesView *axes, QObject *parent = nullptr);

    // 装到绘图对象上：事件过滤器 + 焦点策略。宿主构造末尾调用。
    void attach();

    bool handlePlotEvent(QObject *watched, QEvent *event);
    void keyPress(QKeyEvent *event);
    void contextMenu(QContextMenuEvent *event);

    // 命中测试与像素→时间换算（交互部件的公共入口）。
    AxesView::HitResult hitTest(const QPointF &pixel) const;
    double timeAtPixel(double px) const;

    // ---- 时间书签 ----
    void addBookmark(double t, const QString &label = QString());
    void removeBookmark(const QString &id);
    void renameBookmark(const QString &id, const QString &label);
    const QVector<AxesView::Bookmark> &bookmarks() const { return m_bookmarks; }
    // 读数层需要的"位置 + 文案 + 选中"视图。
    QVector<CursorReadout::Mark> marksForReadout() const;

private:
    void onMouseMove(QMouseEvent *event);
    void onMousePress(QMouseEvent *event);
    void onMouseRelease(QMouseEvent *event);
    void onWheel(QWheelEvent *event);

    AxesView *m_axes = nullptr;
    // 宿主已建好的部件（构造时缓存，交互只读它们，不负责生命周期）。
    nextpilot::plotscope::fastplot::Axes *m_plot = nullptr;
    SeriesStore *m_seriesStore = nullptr;
    CursorReadout *m_readout = nullptr;
    ViewState *m_viewState = nullptr;

    // 时间书签（交互数据，随布局持久化由宿主转存）。
    QVector<AxesView::Bookmark> m_bookmarks;
    QString m_selectedBookmarkId;
    int m_bookmarkSeq = 0;

    // 拖动状态：1=A 2=B 3=书签 4=游标线。
    int m_dragKind = 0;
    QString m_dragBookmarkId;
    QPointF m_pressPos;
    bool m_pressMoved = false;
    int m_legendDragRow = -1; // 图例按下但还没拖动：release 未移动才算"点击切显隐"
};

} // namespace nextpilot::plotscope::gui
