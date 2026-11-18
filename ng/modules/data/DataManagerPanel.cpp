#include "DataManagerPanel.h"

#include "PanelFactoryRegistry.h"
#include "ThemeManager.h"

#include "AppContext.h"
#include "Dataset.h"
#include "DataSource.h"
#include "IIntentExecutor.h"
#include "Intent.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

using namespace ng::core;

namespace ng::gui
{

namespace
{
// 节点角色标记：用于把 Qt 的树节点映射回领域标识。
constexpr int kRoleDatasetId = Qt::UserRole + 1;
constexpr int kRoleTable = Qt::UserRole + 2;
constexpr int kRoleColumn = Qt::UserRole + 3;
constexpr int kRoleSourceId = Qt::UserRole + 4;
// 状态 token 存在 item 的 data 里，供 delegate 取色。
constexpr int kRoleStateToken = Qt::UserRole + 5;

// 源状态的中文文案（§12.6：用户可见文案用中文）。
QString stateText(SourceState state)
{
    switch (state)
    {
    case SourceState::Opening:  return QStringLiteral("打开中");
    case SourceState::Streaming: return QStringLiteral("运行中");
    case SourceState::Paused:   return QStringLiteral("已暂停");
    case SourceState::Closed:   return QStringLiteral("已停止");
    case SourceState::Error:    return QStringLiteral("错误");
    }
    return QStringLiteral("未知");
}

// 源状态对应 token（深浅两套各自可读；§12.5：delegate 动态取色）。
QString stateToken(SourceState state)
{
    switch (state)
    {
    case SourceState::Streaming: return QStringLiteral("stateOk");
    case SourceState::Opening:   return QStringLiteral("accent");
    case SourceState::Paused:    return QStringLiteral("stateWarn");
    case SourceState::Error:     return QStringLiteral("stateError");
    case SourceState::Closed:    break;
    }
    return QStringLiteral("stateIdle");
}

// 源种类文案。
QString kindText(SourceKind kind)
{
    switch (kind)
    {
    case SourceKind::File:   return QStringLiteral("文件");
    case SourceKind::Stream: return QStringLiteral("流");
    case SourceKind::Live:   return QStringLiteral("实时");
    case SourceKind::Replay: return QStringLiteral("回放");
    }
    return QStringLiteral("未知");
}

// 源列表的绘制委托：在第一列画一个**状态圆点**。
//
// 为什么用 delegate 而不是图标资源（§12.5）：
//   状态点要随主题换色，图标 SVG 是固定资产；delegate 在 paint 时按
//   当前主题 token 取色，深浅两套自动都对，且不新增图标资产。
class SourceItemDelegate : public QStyledItemDelegate
{
public:
    explicit SourceItemDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        painter->save();
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        // 状态圆点画在图标位置，文字整体右移让出位置（圆点直径 10 + 边距）。
        opt.icon = QIcon();
        opt.text = QStringLiteral("  ") + opt.text;
        QStyledItemDelegate::paint(painter, opt, index);

        const QString token = index.model()
                                  ? index.model()->data(index, kRoleStateToken).toString()
                                  : QString();
        if (token.isEmpty())
        {
            painter->restore();
            return;
        }
        const QColor c = ThemeManager::themeColor(token);
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setBrush(c);
        painter->setPen(QPen(c.darker(140), 1.0));
        // 直径 10px：8px 在浅色背景上几乎看不见（实测踩过）。
        const QRect r(opt.rect.left() + 5, opt.rect.center().y() - 5, 10, 10);
        painter->drawEllipse(r);
        painter->restore();
    }
};

} // namespace

DataManagerPanel::DataManagerPanel(AppContext &appCtx, IIntentExecutor &executor, QWidget *parent)
    : QWidget(parent), m_appCtx(appCtx), m_repo(appCtx.data()), m_sources(appCtx.sources()),
      m_executor(executor)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(2, 2, 2, 2);
    lay->setSpacing(2);

    // --- 上段：源列表 ---
    m_sourceTree = new QTreeWidget(this);
    m_sourceTree->setHeaderLabels({QStringLiteral("数据源"), QStringLiteral("状态")});
    m_sourceTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_sourceTree->setRootIsDecorated(false);
    m_sourceTree->setItemDelegate(new SourceItemDelegate(this));
    m_sourceTree->setContextMenuPolicy(Qt::CustomContextMenu);
    lay->addWidget(m_sourceTree);
    // 源列表通常只有几行，给它一个上限，别把详情区挤没。
    m_sourceTree->setMaximumHeight(160);

    // --- 过滤框 ---
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(QStringLiteral("搜索数据源 / 表 / 列"));
    m_filter->setClearButtonEnabled(true);
    lay->addWidget(m_filter);

    // --- 下段：详情（数据集 → 表 → 列）+ 空态 ---
    m_stack = new QStackedWidget(this);

    auto *empty = new QWidget(m_stack);
    auto *emptyLay = new QVBoxLayout(empty);
    emptyLay->setContentsMargins(12, 20, 12, 20);
    emptyLay->addStretch(1);
    m_emptyTitle = new QLabel(QStringLiteral("暂无数据源"), empty);
    m_emptyTitle->setAlignment(Qt::AlignCenter);
    m_emptyTitle->setObjectName(QStringLiteral("textDim"));
    m_emptyHint = new QLabel(QStringLiteral("用「打开数据」载入文件，或从脚本调用 data.load 意图"),
                             empty);
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setWordWrap(true);
    m_emptyHint->setObjectName(QStringLiteral("textDim"));
    emptyLay->addWidget(m_emptyTitle);
    emptyLay->addWidget(m_emptyHint);
    emptyLay->addStretch(1);
    m_stack->addWidget(empty);

    m_tree = new QTreeWidget(m_stack);
    m_tree->setHeaderLabels({QStringLiteral("数据集 / 表 / 列"), QStringLiteral("行数")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->setAlternatingRowColors(true);
    m_stack->addWidget(m_tree);
    lay->addWidget(m_stack, 1);

    // --- 交互 ---

    // 选中源 → 详情树展示该源的数据集。
    connect(m_sourceTree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *cur, QTreeWidgetItem *) {
                const int sourceId = cur ? cur->data(0, kRoleSourceId).toInt() : -1;
                if (sourceId == m_shownSourceId)
                    return;
                m_shownSourceId = sourceId;
                rebuildDetails();
                updateEmptyState();
            });

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
                // 活动源 id 是**源**的 id（与数据集 id 是两套空间）：
                // 通过反查得到，重载/导出这类"作用于源"的命令才找得到源。
                const int sourceId = m_sources.sourceOfDataset(datasetId);
                if (sourceId >= 0)
                    m_appCtx.setActiveSourceId(sourceId);
                // 活动通道记在数据集上。
                m_appCtx.setActiveChannel(datasetId, tableName, columnName);
                emit channelActivated(datasetId, tableName, columnName);
            });

    // 过滤：源名 + 表列名。
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &) { applyFilter(); });

    // 源行右键菜单。
    connect(m_sourceTree, &QTreeWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) {
                QTreeWidgetItem *item = m_sourceTree->itemAt(pos);
                if (item)
                    showSourceMenu(item, m_sourceTree->viewport()->mapToGlobal(pos));
            });

    // 数据变化 -> 重建。
    connect(&m_repo, &DataRepository::datasetAdded, this, [this](DatasetId) { refresh(); });
    connect(&m_repo, &DataRepository::datasetRemoved, this, [this](DatasetId) { refresh(); });
    // 源变化（状态推进、进度）-> 只刷源列表（详情树不受影响，除非源没了）。
    connect(&m_sources, &DataSourceRepository::sourceAdded, this, [this](int) { refresh(); });
    connect(&m_sources, &DataSourceRepository::sourceRemoved, this, [this](int) { refresh(); });
    connect(&m_sources, &DataSourceRepository::sourceChanged, this,
            [this](int) { rebuildSources(); });

    refresh();
}

void DataManagerPanel::showSourceMenu(QTreeWidgetItem *item, const QPoint &globalPos)
{
    const int sourceId = item->data(0, kRoleSourceId).toInt();
    const DataSourceInfo *info = m_sources.get(sourceId);
    if (!info)
        return;

    QMenu menu(this);
    // --- 只放有底层支撑的动作 ---
    //
    // 老 dock 的菜单里还有 live 连接/暂停/变换/设备调试等，ng 尚无底层实现，
    // 这里**不做**——不做点了没反应的按钮（§12.6：文案要与实际行为一致）。
    QAction *reload = menu.addAction(QStringLiteral("重载数据源"));
    QAction *exportCsv = menu.addAction(QStringLiteral("导出 CSV…"));
    QAction *clearCache = menu.addAction(QStringLiteral("清除全部解析缓存"));
    menu.addSeparator();
    QAction *activate = menu.addAction(QStringLiteral("设为活动数据源"));

    QAction *chosen = menu.exec(globalPos);
    if (!chosen)
        return;

    if (chosen == reload)
    {
        // 重载：复用 data.load 路径，源 id 显式给出。
        Intent intent;
        intent.op = QStringLiteral("data.reload");
        intent.params[QStringLiteral("id")] = sourceId;
        const auto r = m_executor.execute(intent);
        if (!r)
            QMessageBox::warning(this, QStringLiteral("重载失败"), r.error().message);
    }
    else if (chosen == exportCsv)
    {
        // 导出：需要输出路径（GUI 交互在 GUI 层），其余参数走意图。
        const DataSourceInfo *src = m_sources.get(sourceId);
        if (!src || src->datasetIds.isEmpty())
        {
            QMessageBox::information(this, QStringLiteral("导出 CSV"),
                                     QStringLiteral("该数据源还没有解析出数据集"));
            return;
        }
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("导出 CSV"), QString(),
            QStringLiteral("CSV 文件 (*.csv)"));
        if (path.isEmpty())
            return;
        Intent intent;
        intent.op = QStringLiteral("data.export");
        intent.params[QStringLiteral("dataset")] = src->datasetIds.first();
        intent.params[QStringLiteral("path")] = path;
        const auto r = m_executor.execute(intent);
        if (!r)
            QMessageBox::warning(this, QStringLiteral("导出失败"), r.error().message);
    }
    else if (chosen == clearCache)
    {
        // 清除缓存：当前实现作用于全部源（意图层能力），文案如实说明。
        Intent intent;
        intent.op = QStringLiteral("data.clearCache");
        const auto r = m_executor.execute(intent);
        if (!r)
            QMessageBox::warning(this, QStringLiteral("清除缓存失败"), r.error().message);
    }
    else if (chosen == activate)
    {
        // 设为活动源：这是**状态**切换而非命令，直接改共享状态。
        m_appCtx.setActiveSourceId(sourceId);
    }
}

void DataManagerPanel::refresh()
{
    rebuildSources();
    rebuildDetails();
    updateEmptyState();
}

void DataManagerPanel::rebuildSources()
{
    const bool blocked = m_sourceTree->blockSignals(true);
    // 记住当前选中的源：重建后恢复它（否则刷新一次就跳回第一个源）。
    const int prevSourceId = m_shownSourceId;
    m_sourceTree->clear();

    for (int sourceId : m_sources.ids())
    {
        const DataSourceInfo *info = m_sources.get(sourceId);
        if (!info)
            continue;

        auto *item = new QTreeWidgetItem(m_sourceTree);
        // 显示名用**文件名**（完整路径太长会把列挤爆），路径放 tooltip。
        QString display = info->name;
        if (display.isEmpty())
        {
            display = QFileInfo(info->origin).fileName();
            if (display.isEmpty())
                display = QStringLiteral("(未命名)");
        }
        item->setText(0, display);
        item->setData(0, kRoleSourceId, sourceId);
        item->setData(0, kRoleStateToken, stateToken(info->state));
        item->setText(1, stateText(info->state));

        // 元信息 tooltip：路径、种类、字节进度、解析器绑定、产出的数据集。
        QStringList meta;
        meta << QStringLiteral("来源: %1").arg(info->origin);
        meta << QStringLiteral("种类: %1").arg(kindText(info->kind));
        if (info->bytesTotal > 0)
        {
            meta << QStringLiteral("进度: %1 / %2 字节")
                        .arg(info->bytesRead)
                        .arg(info->bytesTotal);
        }
        if (!info->parsers.isEmpty())
        {
            QStringList pids;
            for (const ParserBinding &b : info->parsers)
                pids << b.parserId;
            meta << QStringLiteral("解析器: %1").arg(pids.join(QStringLiteral(", ")));
        }
        if (!info->datasetIds.isEmpty())
        {
            QStringList dsids;
            for (int d : info->datasetIds)
                dsids << QStringLiteral("#%1").arg(d);
            meta << QStringLiteral("数据集: %1").arg(dsids.join(QStringLiteral(", ")));
        }
        if (!info->error.isEmpty())
            meta << QStringLiteral("错误: %1").arg(info->error);
        item->setToolTip(0, meta.join(QLatin1Char('\n')));
        item->setToolTip(1, item->toolTip(0));
    }

    m_sourceTree->blockSignals(blocked);

    // 恢复选中：优先原选中源；没有则选第一个——面板打开即有内容，
    // 不必用户先点一下才知道"这里该有什么"（对齐老 dock 的默认行为）。
    QTreeWidgetItem *target = nullptr;
    if (prevSourceId >= 0)
    {
        for (int i = 0; i < m_sourceTree->topLevelItemCount(); ++i)
        {
            QTreeWidgetItem *it = m_sourceTree->topLevelItem(i);
            if (it->data(0, kRoleSourceId).toInt() == prevSourceId)
            {
                target = it;
                break;
            }
        }
    }
    if (!target && m_sourceTree->topLevelItemCount() > 0)
        target = m_sourceTree->topLevelItem(0);
    if (target)
    {
        m_sourceTree->setCurrentItem(target);
        const int targetId = target->data(0, kRoleSourceId).toInt();
        if (m_shownSourceId != targetId)
        {
            m_shownSourceId = targetId;
            rebuildDetails();
        }
    }
}

void DataManagerPanel::rebuildDetails()
{
    const bool blocked = m_tree->blockSignals(true);
    m_tree->clear();

    // 详情只展示**当前选中源**的数据集。
    if (m_shownSourceId >= 0)
    {
        const DataSourceInfo *info = m_sources.get(m_shownSourceId);
        if (info)
        {
            for (int dsId : info->datasetIds)
            {
                const Dataset *ds = m_repo.get(dsId);
                if (!ds)
                    continue;

                auto *dsItem = new QTreeWidgetItem(m_tree);
                dsItem->setText(0, QStringLiteral("#%1 %2").arg(dsId).arg(ds->name()));
                dsItem->setData(0, kRoleDatasetId, dsId);
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
                    tItem->setData(0, kRoleDatasetId, dsId);

                    for (const Column &col : table->columns)
                    {
                        auto *cItem = new QTreeWidgetItem(tItem);
                        cItem->setText(0, col.name);
                        cItem->setText(1, col.unit);
                        cItem->setData(0, kRoleColumn, col.name);
                        cItem->setData(0, kRoleTable, table->name);
                        cItem->setData(0, kRoleDatasetId, dsId);
                    }
                    tItem->setExpanded(true);
                }
                dsItem->setExpanded(true);
            }
        }
    }

    m_tree->blockSignals(blocked);
    applyFilter();
}

void DataManagerPanel::applyFilter()
{
    const QString needle = m_filter->text().trimmed();
    const bool empty = needle.isEmpty();

    // 源列表：按源名过滤。
    for (int i = 0; i < m_sourceTree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *item = m_sourceTree->topLevelItem(i);
        const bool hit = empty || item->text(0).contains(needle, Qt::CaseInsensitive);
        item->setHidden(!hit);
    }

    // 详情树：按表名/列名过滤（保留命中的祖先链）。
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *dsItem = m_tree->topLevelItem(i);
        bool dsVisible = false;
        for (int t = 0; t < dsItem->childCount(); ++t)
        {
            QTreeWidgetItem *tItem = dsItem->child(t);
            const bool tableHit =
                empty || tItem->text(0).contains(needle, Qt::CaseInsensitive);
            bool anyCol = false;
            for (int c = 0; c < tItem->childCount(); ++c)
            {
                QTreeWidgetItem *cItem = tItem->child(c);
                const bool hit = empty || tableHit ||
                                 cItem->text(0).contains(needle, Qt::CaseInsensitive);
                cItem->setHidden(!hit);
                anyCol = anyCol || hit;
            }
            tItem->setHidden(!anyCol);
            dsVisible = dsVisible || anyCol;
        }
        dsItem->setHidden(!dsVisible);
    }
}

void DataManagerPanel::updateEmptyState()
{
    const bool hasSource = m_sources.count() > 0;
    if (!hasSource)
    {
        m_stack->setCurrentIndex(0); // 空态
        m_emptyTitle->setText(QStringLiteral("暂无数据源"));
        return;
    }
    if (m_shownSourceId < 0)
    {
        // 有源但没选中：详情区提示选一个源（不是空态页）。
        m_stack->setCurrentIndex(0);
        m_emptyTitle->setText(QStringLiteral("请选择一个数据源"));
        m_emptyHint->setText(QStringLiteral("在上方列表中选中数据源，这里会显示它的表与列"));
        return;
    }
    m_stack->setCurrentIndex(1); // 详情
}

int DataManagerPanel::sourceNodeCount() const
{
    return m_sourceTree->topLevelItemCount();
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

QString DataManagerPanel::sourceStateText(int row) const
{
    QTreeWidgetItem *item = m_sourceTree->topLevelItem(row);
    return item ? item->text(1) : QString();
}

QWidget *createDatasetTreePanel(AppContext &appCtx, IIntentExecutor &exec, QWidget *parent)
{
    return new DataManagerPanel(appCtx, exec, parent);
}

// 视图层自注册：外壳与其它功能都不认识这个面板。
NG_REGISTER_PANEL("dataset-tree", datasetTree, createDatasetTreePanel)

} // namespace ng::gui
