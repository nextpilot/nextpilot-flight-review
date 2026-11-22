#include "DataTableModel.h"

#include <QElapsedTimer>

#include <cmath>

namespace nextpilot::plotscope::data
{

using nextpilot::plotscope::dataset::Dataset;

const DataTableModel::Page DataTableModel::kEmptyPage;

DataTableModel::DataTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
    m_pages.setMaxCost(64); // 64 页 LRU：10 列 × 1024 行约 5MB 常驻上限
}

void DataTableModel::setDataset(const Dataset *ds, const QString &table)
{
    beginResetModel();
    m_ds = ds;
    m_tableName = table;
    m_table = (ds && !table.isEmpty()) ? ds->table(table) : nullptr;
    m_pages.clear();
    m_pageFetches = 0;
    m_lastPageMs = 0;
    endResetModel();
}

int DataTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_table)
        return 0;
    return m_table->rows;
}

int DataTableModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_table)
        return 0;
    return m_table->columns.size() + 1; // 时间列 + 数据列
}

const DataTableModel::Page &DataTableModel::page(int pageIndex) const
{
    if (!m_table || !m_ds || pageIndex < 0)
        return kEmptyPage;
    if (Page *hit = m_pages.object(pageIndex))
        return *hit;

    QElapsedTimer timer;
    timer.start();
    auto *p = new Page;
    const int rowStart = pageIndex * m_pageSize;
    const int maxRows = qMin(m_pageSize, m_table->rows - rowStart);
    p->rows = qMax(0, maxRows);
    p->cols.resize(m_table->columns.size());

    // 时间轴：typed 表所有列共享同一轴，readTimeRows 从首列取。
    m_ds->readTimeRows(m_tableName, rowStart, p->rows, p->time);
    for (int c = 0; c < m_table->columns.size(); ++c)
        m_ds->readColumnRows(m_tableName, m_table->columns.at(c).name,
                             rowStart, p->rows, p->cols[c]);

    m_pages.insert(pageIndex, p, 1);
    ++m_pageFetches;
    m_lastPageMs = timer.elapsed();
    return *p;
}

QVariant DataTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || !m_table || role != Qt::DisplayRole)
        return {};
    const int row = index.row();
    if (row < 0 || row >= m_table->rows)
        return {};
    const Page &pg = page(row / m_pageSize);
    const int inPage = row % m_pageSize;
    if (inPage >= pg.rows)
        return {};

    if (index.column() == 0)
        return QString::number(pg.time.at(inPage), 'f', 6); // 秒，µs 精度

    const int c = index.column() - 1;
    if (c < 0 || c >= pg.cols.size())
        return {};
    const double v = pg.cols.at(c).at(inPage);
    if (std::isnan(v))
        return QString(); // null 显示为空
    return QString::number(v, 'g', 10);
}

QVariant DataTableModel::headerData(int section, Qt::Orientation orientation,
                                    int role) const
{
    if (role != Qt::DisplayRole || !m_table)
        return QAbstractTableModel::headerData(section, orientation, role);
    if (orientation == Qt::Vertical)
        return section + 1; // 行号从 1 起，对齐 Excel 习惯
    if (section == 0)
        return QStringLiteral("时间(s)");
    const int c = section - 1;
    if (c < 0 || c >= m_table->columns.size())
        return {};
    const auto &col = m_table->columns.at(c);
    return col.unit.isEmpty() ? col.name
                              : QStringLiteral("%1 [%2]").arg(col.name, col.unit);
}

} // namespace nextpilot::plotscope::data
