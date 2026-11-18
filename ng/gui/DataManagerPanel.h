#pragma once

#include "DataRepository.h"

#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;

// 数据集管理面板：左侧停靠区的树视图。
//
// 职责单一：展示"仓库里有哪些数据集、每张表有哪些列"，并把用户的
// 选列动作以信号抛给上层。不认识格式、不做分析、不自己取数绘图。
//
// 数据来源是 DataRepository（唯一数据所有者）；面板只读元数据，
// 因此百万行数据集在树上同样只是一行摘要，不占内存。
namespace ng::gui
{

class DataManagerPanel : public QWidget
{
    Q_OBJECT
public:
    explicit DataManagerPanel(ng::core::DataRepository &repo, QWidget *parent = nullptr);

    // 重建整棵树（仓库变化时调用）。
    void refresh();

    // 诊断/测试用：树里的数据集节点数、某数据集下的表节点数。
    int datasetNodeCount() const;
    int tableNodeCount(int datasetIndex) const;

signals:
    // 用户在树上双击某个通道时发出：数据集 id、表名、列名。
    void channelActivated(int datasetId, const QString &table, const QString &column);

private:
    void rebuild();

    ng::core::DataRepository &m_repo;
    QTreeWidget *m_tree = nullptr;
};

} // namespace ng::gui
