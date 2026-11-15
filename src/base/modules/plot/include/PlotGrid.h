#pragma once

#include <QGridLayout>
#include <QWidget>
#include <QVector>
#include <algorithm>

class CanvasWidget;

namespace ps::plot { class PlotWorkspace; }

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


void collectPageCanvases(QWidget *root, QVector<CanvasWidget *> &out);
QWidget *buildGridSplitter(const QVector<CanvasWidget *> &canvases, int rows, int cols, QWidget *parent);
