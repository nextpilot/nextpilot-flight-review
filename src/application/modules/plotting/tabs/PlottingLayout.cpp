#include "PlottingLayout.h"
#include "PlottingCanvas.h"

#include <QTimer>
#include <algorithm>

namespace ps::plotting
{

PlottingLayout::PlottingLayout(Qt::Orientation orientation, QWidget *parent)
    : QSplitter(orientation, parent)
{
    setHandleWidth(6);
    setChildrenCollapsible(false);
}

PlottingLayout *PlottingLayout::fromGrid(const QVector<PlottingCanvas *> &canvases,
                                         int rows, int cols, QWidget *parent)
{
    rows = std::clamp(rows, 1, 8);
    cols = std::clamp(cols, 1, 8);
    while (canvases.size() > rows * cols && rows < 8)
        ++rows;
    auto *root = new PlottingLayout(Qt::Vertical, parent);
    for (int r = 0; r < rows; ++r)
    {
        auto *row = new PlottingLayout(Qt::Horizontal, root);
        for (int c = 0; c < cols; ++c)
        {
            const int i = r * cols + c;
            if (i < canvases.size())
                row->addCanvas(canvases.at(i));
        }
        root->addWidget(row);
    }
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
}

void PlottingLayout::removeCanvas(PlottingCanvas *canvas)
{
    if (!canvas || indexOf(canvas) < 0)
        return;
    canvas->setParent(nullptr);
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
    rows = std::clamp(rows, 1, 8);
    cols = std::clamp(cols, 1, 8);
    QVector<PlottingCanvas *> leaves = canvases();
    while (leaves.size() > rows * cols && rows < 8)
        ++rows;
    m_rows = rows;
    m_cols = cols;
    clearChildren();
    for (int r = 0; r < rows; ++r)
    {
        auto *row = new PlottingLayout(Qt::Horizontal, this);
        for (int c = 0; c < cols; ++c)
        {
            const int i = r * cols + c;
            if (i < leaves.size())
                row->addCanvas(leaves.at(i));
        }
        addWidget(row);
    }
    QTimer::singleShot(0, this, [this] {
        QList<int> sizes;
        for (int i = 0; i < count(); ++i)
            sizes.append(1);
        setSizes(sizes);
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
        parent->insertWidget(at + 1, canvas);
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
