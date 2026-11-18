#include "DataManagerPanel.h"

#include "PanelFactoryRegistry.h"

#include "AppContext.h"
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

DataManagerPanel::DataManagerPanel(AppContext &appCtx, QWidget *parent)
    : QWidget(parent), m_appCtx(appCtx), m_repo(appCtx.data())
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(2, 2, 2, 2);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({QStringLiteral("数据集 / 表 / 列"), QStringLiteral("行数")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->setAlternatingRowColors(true);
    lay->addWidget(m_tree);

    // 双击某个列节点 = 用户想看这条通道。
    //
    // 协作方式：**记到 AppContext 的共享状态**，而不是直接调用别的视图。
    // 于是数据集树不认识绘图视图，绘图视图也不认识数据集树——
    // 两者只是都订阅了同一份"当前活动通道"。加第三个视图时，
    // 它只要订阅这个信号即可，谁都不用改。
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) {
                if (!item)
                    return;
                const QVariant dsId = item->data(0, kRoleDatasetId);
                const QVariant table = item->data(0, kRoleTable);
                const QVariant column = item->data(0, kRoleColumn);
                if (!dsId.isValid() || !table.isValid() || !column.isValid())
                    return;
                const int datasetId = dsId.toInt();
                const QString tableName = table.toString();
                const QString columnName = column.toString();
                // 共享状态：活动数据集 + 活动通道
                m_appCtx.setActiveSourceId(datasetId);
                m_appCtx.setActiveChannel(datasetId, tableName, columnName);
                emit channelActivated(datasetId, tableName, columnName);
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

QWidget *createDatasetTreePanel(AppContext &appCtx, IIntentExecutor &exec, QWidget *parent)
{
    (void)exec; // 数据集树不主动取数，只读元数据
    return new DataManagerPanel(appCtx, parent);
}

// 视图层自注册：外壳与其它功能都不认识这个面板。
NG_REGISTER_PANEL("dataset-tree", datasetTree, createDatasetTreePanel)

} // namespace ng::gui
