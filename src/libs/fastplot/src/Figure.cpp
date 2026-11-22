#include "fastplot/Figure.h"

#include <QtGlobal>
#include <QSize>

namespace nextpilot::plotscope::fastplot
{

Figure::Figure(QWidget *parent)
    : QWidget(parent)
    , m_grid(new QGridLayout(this))
{
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setSpacing(4);
    m_slots.fill(-1, m_rows * m_cols);
    // 默认一个坐标系，覆盖整块区域——单图场景开箱即用。
    place(addAxes(), 0, 0);
}

Axes *Figure::addAxes()
{
    auto *a = new Axes(this);
    m_axes.append(a);
    return a;
}

void Figure::place(Axes *axes, int row, int col)
{
    if (!axes)
        return;
    m_grid->addWidget(axes, row, col);
}

Axes *Figure::subplot(int rows, int cols, int index)
{
    rows = qMax(1, rows);
    cols = qMax(1, cols);
    index = qBound(0, index, rows * cols - 1);

    // 网格规模变化时重建槽位表，并把已有坐标系重新摆位。
    if (rows != m_rows || cols != m_cols)
    {
        m_rows = rows;
        m_cols = cols;
        m_slots.fill(-1, rows * cols);
        for (int i = 0; i < m_axes.size() && i < m_slots.size(); ++i)
        {
            m_slots[i] = i;                       // 已有坐标系占据对应槽位
            place(m_axes.at(i), i / cols, i % cols);
        }
    }

    if (m_slots.at(index) < 0)
    {
        Axes *a = addAxes();
        m_slots[index] = m_axes.size() - 1;
        place(a, index / cols, index % cols);
    }
    return m_axes.at(m_slots.at(index));
}

void Figure::clear()
{
    // 复用已有坐标系清空内容，避免反复 new/delete 引起的布局抖动。
    for (Axes *a : m_axes)
        a->clearCurves();
}

} // namespace nextpilot::plotscope::fastplot
