#pragma once

#include "fastplot/Axes.h"

#include <QGridLayout>
#include <QVector>
#include <QWidget>

namespace nextpilot::plotscope::fastplot
{

// Figure —— 顶层绘图窗口。术语对齐 MATLAB 的 figure / matplotlib 的 Figure。
//
// 一个 Figure 容纳一到多个 Axes（坐标系）。单 Axes 时用 axes()/gca() 取默认坐标系；
// 多 Axes 时用 subplot(rows, cols, index) 按网格摆放（index 从 0 起，行优先）。
//
// 与老架构的关系：老的 PlotWidget 一个类兼了 figure 和 axes 两个角色；现在拆开——
// Figure 负责"窗口里有几个坐标系、怎么摆"，Axes 负责"一个坐标系怎么画"。
// PlotCanvas / CanvasWorkspace 仍可直接持有一个 Axes（单图场景无需 Figure）。
class Figure : public QWidget
{
    Q_OBJECT
public:
    explicit Figure(QWidget *parent = nullptr);

    // 默认（第一个）坐标系。Figure 构造时即创建一个，故永不为空。
    Axes *axes() const { return m_axes.isEmpty() ? nullptr : m_axes.first(); }
    Axes *gca() const { return axes(); }   // MATLAB gca 风格别名

    // 新建一个坐标系并追加到网格末尾（自动选下一个空位）。
    Axes *addAxes();

    // MATLAB subplot 风格：rows 行 cols 列，index 从 0 起行优先。
    // 重复占用同一格时复用该格的 Axes；返回该格坐标系。
    Axes *subplot(int rows, int cols, int index);

    Axes *axesAt(int index) const { return m_axes.value(index, nullptr); }
    int axesCount() const { return m_axes.size(); }

    // 清空所有坐标系内容（不删除 Figure 本身）。
    void clear();

private:
    // 把已有 Axes 放到网格的 row/col；越界索引会被夹取。
    void place(Axes *axes, int row, int col);

    QGridLayout *m_grid = nullptr;
    QVector<Axes *> m_axes;
    // 网格槽位 -> axes 在 m_axes 里的下标；-1 表示空。
    QVector<int> m_slots;
    int m_rows = 1;
    int m_cols = 1;
};

} // namespace nextpilot::plotscope::fastplot
