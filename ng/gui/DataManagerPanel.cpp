#include "DataManagerPanel.h"
#include "Dataset.h"
#include <QHeaderView>
#include <QVBoxLayout>
#include <QTreeWidget>
#include <QTreeWidgetItem>



using namespace ng::core;

namespace ng::gui
{


namespace
{
// 节点角色标记：用于把 Qt 的树节点映射回领域标识。
constexpr int kRoleDatasetId = Qt::UserRole + 1;
constexpr int kRoleTable = Qt::UserRole + 2;
constexpr int kRoleColumn = Qt::UserRole + 3;
} // namespace

DataManagerPanel::DataManagerPanel(DataRepository &repo, QWidget *parent)
    : QWidget(parent), m_repo(repo)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(2, 2, 2, 2);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({QStringLiteral("数据集 / 表 / 列"), QStringLiteral("行数")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->setAlternatingRowColors(true);
    lay->addWidget(m_tree);

    // 双击某个列节点 = 用户想看这条通道。
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) {
                if (!item)
                    return;
                const QVariant dsId = item->data(0, kRoleDatasetId);
                const QVariant table = item->data(0, kRoleTable);
                const QVariant column = item->data(0, kRoleColumn);
                if (!dsId.isValid() || !table.isValid() || !column.isValid())
                    return;
                emit channelActivated(dsId.toInt(), table.toString(), column.toString());
            });

    // 仓库变化 -> 重建树。
    connect(&m_repo, &DataRepository::datasetAdded, this, [this](DatasetId) { rebuild(); });
    connect(&m_repo, &DataRepository::datasetRemoved, this, [this](DatasetId) { rebuild(); });

    rebuild();
}

void DataManagerPanel::rebuild()
{
    const bool blocked = m_tree->blockSignals(true);
    m_tree->clear();

    for (DatasetId id : m_repo.ids())
    {
        const Dataset *ds = m_repo.get(id);
        if (!ds)
            continue;

        auto *dsItem = new QTreeWidgetItem(m_tree);
        dsItem->setText(0, QStringLiteral("#%1 %2").arg(id).arg(ds->name()));
        dsItem->setData(0, kRoleDatasetId, id);
        dsItem->setFirstColumnSpanned(true);

        for (int t = 0; t < ds->tableCount(); ++t)
        {
            const Table *table = ds->table(t);
            if (!table)
                continue;
            auto *tItem = new QTreeWidgetItem(dsItem);
            tItem->setText(0, table->name);
            tItem->setText(1, QString::number(table->rows));
            tItem->setData(0, kRoleTable, table->name);
            tItem->setData(0, kRoleDatasetId, id);

            for (const Column &col : table->columns)
            {
                auto *cItem = new QTreeWidgetItem(tItem);
                cItem->setText(0, col.name);
                cItem->setText(1, col.unit);
                cItem->setData(0, kRoleColumn, col.name);
                cItem->setData(0, kRoleTable, table->name);
                cItem->setData(0, kRoleDatasetId, id);
            }
            tItem->setExpanded(true);
        }
        dsItem->setExpanded(true);
    }

    m_tree->blockSignals(blocked);
}

void DataManagerPanel::refresh()
{
    rebuild();
}

int DataManagerPanel::datasetNodeCount() const
{
    return m_tree->topLevelItemCount();
}

int DataManagerPanel::tableNodeCount(int datasetIndex) const
{
    QTreeWidgetItem *item = m_tree->topLevelItem(datasetIndex);
    return item ? item->childCount() : 0;
}

} // namespace ng::gui
