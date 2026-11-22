#pragma once

// DataTableModel：数据表格查看器的懒加载模型。
//
// 设计铁律（对齐 Dataset 门面的百万级承诺）：
//   - C++ 常驻内存 = O(可见页)：按页从 Rust kernel 取数（readTimeRows/readColumnRows），
//     页缓存在 QCache 里 LRU 淘汰；百万行只发生在 kernel 的列存里。
//   - data() 只做查页 + 格式化，不触发任何 O(行数) 的工作——翻页/跳转 <100ms 是硬验收线。
//   - 时间列固定第 0 列；其余列按 schema 顺序。null 单元显示为空串（值本身是 NaN）。
//
// 纯 QtCore：不碰 Widgets / 不注册面板——headless 测试可以直接实例化。

#include "Dataset.h"

#include <QAbstractTableModel>
#include <QCache>
#include <QElapsedTimer>
#include <QString>
#include <QVector>

namespace nextpilot::plotscope::data
{

class DataTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit DataTableModel(QObject *parent = nullptr);

    // 绑定数据集的某张表（只读快照）；ds 生命周期须长于模型（仓库持有）。
    // table 为空串 = 解绑（空模型）。
    void setDataset(const nextpilot::plotscope::dataset::Dataset *ds,
                    const QString &table);

    // --- 诊断/验收 ---
    int pageFetchCount() const { return m_pageFetches; }   // 实际发生的页读取次数
    qint64 lastPageMs() const { return m_lastPageMs; }     // 最近一次页读取耗时
    int pageSize() const { return m_pageSize; }
    QString tableName() const { return m_tableName; }

    // --- QAbstractTableModel ---
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role) const override;

private:
    struct Page
    {
        QVector<double> time;              // 秒
        QVector<QVector<double>> cols;     // 与 schema 列一一对应
        int rows = 0;                      // 实际读到的行数（页尾可能不足页大小）
    };
    // 懒加载取页：未命中时一次性读整页（时间 + 全部列），计一次 fetch。
    const Page &page(int pageIndex) const;

    const nextpilot::plotscope::dataset::Dataset *m_ds = nullptr;
    const nextpilot::plotscope::dataset::Table *m_table = nullptr;
    QString m_tableName;
    int m_pageSize = 1024;
    // cost=1/页；容量按页数算，页内内存 = 1024 行 × 列数 × 8B（10 列约 80KB）。
    mutable QCache<int, Page> m_pages;
    mutable int m_pageFetches = 0;
    mutable qint64 m_lastPageMs = 0;
    static const Page kEmptyPage;
};

} // namespace nextpilot::plotscope::data
