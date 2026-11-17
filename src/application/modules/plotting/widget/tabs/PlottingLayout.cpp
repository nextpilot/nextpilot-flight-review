#include "PlottingLayout.h"
#include "PlottingCanvas.h"

#include <QTimer>
#include <algorithm>

namespace ps::plotting
{

PlottingLayout *PlottingLayout::layoutOf(QWidget *page)
{
    if (auto *layout = dynamic_cast<PlottingLayout *>(page))
        return layout;
    if (auto *p = dynamic_cast<PlottingPage *>(page))
        return p->layout();
    return nullptr;
}

PlottingPage::PlottingPage(QWidget *parent) : QScrollArea(parent)
{
    setObjectName(QStringLiteral("plotPage"));
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_layout = new PlottingLayout(Qt::Vertical, this);
    setWidget(m_layout);
}

PlottingLayout::PlottingLayout(Qt::Orientation orientation, QWidget *parent)
    : QSplitter(orientation, parent)
{
    setHandleWidth(6);
    setChildrenCollapsible(false);
}

PlottingLayout *PlottingLayout::fromGrid(const QVector<PlottingCanvas *> &canvases,
                                         int rows, int cols, QWidget *parent)
{
    auto *root = new PlottingLayout(Qt::Vertical, parent);
    for (PlottingCanvas *c : canvases)
        root->addCanvas(c);
    root->setGrid(rows, cols);
    return root;
}

QVector<PlottingCanvas *> PlottingLayout::collectCanvases(QWidget *root)
{
    QVector<PlottingCanvas *> out;
    if (!root)
        return out;
    if (auto *canvas = dynamic_cast<PlottingCanvas *>(root))
    {
        out.append(canvas);
        return out;
    }
    if (auto *splitter = qobject_cast<QSplitter *>(root))
    {
        for (int i = 0; i < splitter->count(); ++i)
            out += collectCanvases(splitter->widget(i));
        return out;
    }
    const auto children = root->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget *child : children)
        out += collectCanvases(child);
    return out;
}

QVector<PlottingCanvas *> PlottingLayout::canvases() const
{
    QVector<PlottingCanvas *> out;
    for (int i = 0; i < count(); ++i)
        out += collectCanvases(widget(i));
    return out;
}

void PlottingLayout::addCanvas(PlottingCanvas *canvas)
{
    if (!canvas || canvases().contains(canvas))
        return;
    addWidget(canvas);
    canvas->show();  // setParent(nullptr) 会隐藏控件，重新挂回后要显式显示
}

void PlottingLayout::removeCanvas(PlottingCanvas *canvas)
{
    if (!canvas)
        return;
    canvas->setParent(nullptr);
    reflow();
}

void PlottingLayout::clearChildren()
{
    QVector<PlottingCanvas *> leaves = canvases();
    for (PlottingCanvas *canvas : leaves)
        canvas->setParent(nullptr);
    while (count() > 0)
    {
        QWidget *child = widget(0);
        child->setParent(nullptr);
        if (!dynamic_cast<PlottingCanvas *>(child))
            delete child;
    }
}

void PlottingLayout::setGrid(int rows, int cols)
{
    m_minRows = std::clamp(rows, 1, 8);
    m_cols = std::clamp(cols, 1, 8);
    m_gridMode = true;
    m_rows = m_minRows;
    rebuildGrid();
}

void PlottingLayout::setCanvases(const QVector<PlottingCanvas *> &ordered)
{
    clearChildren();
    for (PlottingCanvas *c : ordered)
    {
        if (!c)
            continue;
        c->setParent(this);
        addWidget(c);
        c->show();
    }
}

void PlottingLayout::reflow()
{
    if (m_gridMode)
        rebuildGrid();
}

namespace
{
constexpr int kRowMinHeight = 200;
} // namespace

void PlottingLayout::rebuildGrid()
{
    QVector<PlottingCanvas *> leaves = canvases();
    m_rows = std::max(m_minRows, static_cast<int>((leaves.size() + m_cols - 1) / m_cols));
    clearChildren();
    QVector<QSplitter *> rows;
    for (int r = 0; r < m_rows; ++r)
    {
        auto *row = new PlottingLayout(Qt::Horizontal, this);
        row->setMinimumHeight(kRowMinHeight);
        for (int c = 0; c < m_cols; ++c)
        {
            const int i = r * m_cols + c;
            if (i < leaves.size())
            {
                leaves.at(i)->setMinimumWidth(120);  // 允许挤窄，避免格子横出可视区
                row->addCanvas(leaves.at(i));
            }
        }
        addWidget(row);
        rows.append(row);
    }
    // 行数超过一屏时让页容器出竖直滚动条
    setMinimumHeight(m_rows * kRowMinHeight);
    QTimer::singleShot(0, this, [this, rows] {
        QList<int> sizes;
        for (int i = 0; i < count(); ++i)
            sizes.append(1);
        setSizes(sizes);
        // QSplitter 默认按 sizeHint 分配，空白画布会被挤成 0 宽；每行强制均分
        for (QSplitter *row : rows)
        {
            QList<int> rs;
            for (int i = 0; i < row->count(); ++i)
                rs.append(1);
            if (!rs.isEmpty())
                row->setSizes(rs);
        }
    });
}

void PlottingLayout::split(PlottingCanvas *target, Qt::Orientation orientation,
                           PlottingCanvas *canvas)
{
    if (!target || !canvas)
        return;
    QSplitter *parent = qobject_cast<QSplitter *>(target->parentWidget());
    if (!parent)
    {
        addWidget(target);
        addWidget(canvas);
        return;
    }
    const int at = parent->indexOf(target);
    if (at < 0)
    {
        parent->addWidget(canvas);
        return;
    }
    if (parent->orientation() == orientation)
    {
        // 只让被拆分的那一块对半分，同级的其他画布位置和尺寸不动
        QList<int> sizes = parent->sizes();
        const int half = sizes.value(at, 1) / 2;
        parent->insertWidget(at + 1, canvas);
        if (!sizes.isEmpty())
        {
            sizes[at] = half > 0 ? half : 1;
            sizes.insert(at + 1, half > 0 ? half : 1);
            parent->setSizes(sizes);
        }
    }
    else
    {
        auto *nested = new PlottingLayout(orientation, parent);
        parent->insertWidget(at, nested);
        nested->addCanvas(target);
        nested->addCanvas(canvas);
        QTimer::singleShot(0, nested, [nested] { nested->setSizes({1, 1}); });
    }
}

} // namespace ps::plotting
