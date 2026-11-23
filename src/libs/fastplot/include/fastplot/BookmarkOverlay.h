#pragma once

#include "fastplot/Overlay.h"

namespace nextpilot::plotscope::fastplot
{

// 时间书签（对齐老 PlottingCanvas：竖线 + 顶部 chip + 选中高亮）。
struct BookmarkMark
{
    QString id;
    double t = 0.0;
    QString label;
    bool selected = false;
};

// 书签叠加层：状态由 Axes::setBookmarks 喂入，渲染自持。
class BookmarkOverlay : public Overlay
{
public:
    void paint(QPainter &painter, const OverlayContext &ctx) override;

    const QVector<BookmarkMark> &marks() const { return m_marks; }
    void setMarks(const QVector<BookmarkMark> &marks) { m_marks = marks; }

private:
    QVector<BookmarkMark> m_marks;
};

} // namespace nextpilot::plotscope::fastplot
