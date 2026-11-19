#include "DataManagerPanel.h"

#include "PanelFactoryRegistry.h"
#include "ThemeManager.h"
#include "ChannelMime.h"

#include "AppContext.h"
#include "Dataset.h"
#include "DataSource.h"
#include "IIntentExecutor.h"
#include "Intent.h"
#include "PropertySchema.h"
#include "TransformEditorDialog.h"

#include <QAbstractItemView>
#include <QDrag>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

using namespace nextpilot::plotscope::core;

namespace nextpilot::plotscope::gui
{

namespace
{
constexpr int kRoleDatasetId = Qt::UserRole + 1;
constexpr int kRoleTable = Qt::UserRole + 2;
constexpr int kRoleColumn = Qt::UserRole + 3;
constexpr int kRoleSourceId = Qt::UserRole + 4;
constexpr int kRoleNodeKind = Qt::UserRole + 5; // "source"/"dataset"/"table"/"column"/"param"/"msg"

QString stateText(SourceState state)
{
    switch (state)
    {
    case SourceState::Opening:  return QStringLiteral("打开中");
    case SourceState::Streaming: return QStringLiteral("运行中");
    case SourceState::Paused:   return QStringLiteral("已暂停");
    case SourceState::Closed:   return QStringLiteral("已加载");
    case SourceState::Error:    return QStringLiteral("错误");
    }
    return QStringLiteral("未知");
}

// 列节点可拖拽：只发逻辑标识（ChannelMime），不暴露 Dataset 内部结构。
class ChannelTreeWidget : public QTreeWidget
{
public:
    explicit ChannelTreeWidget(QWidget *parent = nullptr) : QTreeWidget(parent) {}

protected:
    void startDrag(Qt::DropActions supportedActions) override
    {
        QTreeWidgetItem *item = currentItem();
        if (!item)
            return;
        const QVariant dsId = item->data(0, kRoleDatasetId);
        const QVariant table = item->data(0, kRoleTable);
        const QVariant column = item->data(0, kRoleColumn);
        if (!dsId.isValid() || !table.isValid() || !column.isValid())
            return;
        auto *mime = new QMimeData;
        const QString payload = channel_mime::encode(dsId.toInt(), table.toString(), column.toString());
        mime->setData(QString::fromLatin1(channel_mime::type()), payload.toUtf8());
        mime->setText(table.toString() + QLatin1Char('.') + column.toString());
        auto *drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->exec(supportedActions, Qt::CopyAction);
    }
};

struct DeriveRecipe
{
    const char *id;
    const char *label;
    const char *params;
};
const DeriveRecipe kDeriveRecipes[] = {
    {"absolute",       "绝对值",     "{}"},
    {"normalize",      "归一化",     "{}"},
    {"scale",          "缩放/偏移",  "{\"value_scale\":1,\"value_offset\":0}"},
    {"derivative",     "微分",       "{}"},
    {"integral",       "积分",       "{}"},
    {"moving_average", "滑动平均",   "{\"window\":10}"},
    {"moving_rms",     "滑动均方根", "{\"window\":10}"},
    {"moving_variance","滑动方差",   "{\"window\":10}"},
};

bool isKeyValueTable(const Table *t)
{
    if (!t || t->rows <= 0 || t->columns.isEmpty())
        return false;
    for (const Column &c : t->columns)
        if (c.type == ValueType::Text)
            return true;
    return false;
}

} // namespace

DataManagerPanel::DataManagerPanel(AppContext &appCtx, IIntentExecutor &executor, QWidget *parent)
    : QWidget(parent), m_appCtx(appCtx), m_repo(appCtx.data()), m_sources(appCtx.sources()),
      m_executor(executor)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_header = new QLabel(QStringLiteral("数据源"), this);
    m_header->setObjectName(QStringLiteral("sectionTitle"));
    m_header->setContentsMargins(8, 6, 8, 4);
    lay->addWidget(m_header);

    // chip：字段 / 参数 / 消息（切换树显示哪类内容，抄老代码）。
    auto *chipHost = new QWidget(this);
    auto *chipLay = new QHBoxLayout(chipHost);
    chipLay->setContentsMargins(6, 2, 6, 2);
    chipLay->setSpacing(4);
    const QString chipQss = QStringLiteral(
        "QToolButton#sourceChip{background:palette(button);border:1px solid palette(mid);"
        "border-radius:9px;color:palette(button-text);font-size:11px;padding:2px 8px;}"
        "QToolButton#sourceChip:hover{border-color:palette(highlight);}"
        "QToolButton#sourceChip:checked{background:palette(highlight);color:palette(highlighted-text);"
        "border-color:palette(highlight);}");
    const char *chipLabels[3] = {"字段 0", "参数 0", "消息 0"};
    for (int i = 0; i < 3; ++i)
    {
        auto *chip = new QToolButton(chipHost);
        chip->setCheckable(true);
        chip->setChecked(i == 0);
        chip->setText(QString::fromLatin1(chipLabels[i]));
        chip->setCursor(Qt::PointingHandCursor);
        chip->setObjectName(QStringLiteral("sourceChip"));
        chip->setStyleSheet(chipQss);
        chip->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        chip->setProperty("chipIndex", i);
        m_chips[i] = chip;
        chipLay->addWidget(chip, 1);
        connect(chip, &QToolButton::clicked, this, [this, i] { selectChip(i); });
    }
    lay->addWidget(chipHost);

    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(QStringLiteral("搜索字段 / 参数 / 消息"));
    m_filter->setClearButtonEnabled(true);
    m_filter->setObjectName(QStringLiteral("searchBox"));
    m_filter->setContentsMargins(6, 0, 6, 0);
    lay->addWidget(m_filter);
    connect(m_filter, &QLineEdit::textChanged, this, &DataManagerPanel::applyFilter);

    // 统一数据源树：每行一个源，展开后 dataset → 表 → 列。
    m_tree = new ChannelTreeWidget(this);
    m_tree->setHeaderLabels({QStringLiteral("数据源 / 数据集 / 字段"), QStringLiteral("信息")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->setAlternatingRowColors(true);
    m_tree->setDragEnabled(true);
    m_tree->setDragDropMode(QAbstractItemView::DragOnly);
    m_tree->setDefaultDropAction(Qt::CopyAction);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setIndentation(14);
    lay->addWidget(m_tree, 1);

    // 空态：无源时显示，树隐藏。
    m_empty = new QLabel(this);
    m_empty->setText(QStringLiteral("还没有数据源\n打开飞行日志，或连接一个数据源开始分析"));
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    m_empty->setObjectName(QStringLiteral("textDim"));
    lay->addWidget(m_empty, 1);

    // 树：双击列节点 = 激活通道；单击源节点 = 展开/选中；右键 = 上下文菜单。
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
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
        const int sourceId = m_sources.sourceOfDataset(datasetId);
        if (sourceId >= 0)
            m_appCtx.setActiveSourceId(sourceId);
        m_appCtx.setActiveChannel(datasetId, tableName, columnName);
        emit channelActivated(datasetId, tableName, columnName);
    });
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int col) {
        if (!item || col != 0)
            return;
        // 统一选中：任何节点单击都上报，属性面板据此刷新。
        reportSelection(item);
        const QVariant sid = item->data(0, kRoleSourceId);
        if (!sid.isValid())
            return;
        const int sourceId = sid.toInt();
        if (sourceId != m_shownSourceId)
            selectSource(sourceId);
    });
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QTreeWidgetItem *item = m_tree->itemAt(pos);
        if (!item)
        {
            // 空白区右键：最近文件菜单（抄老代码空态）。
            showRecentFilesMenu(m_tree->viewport()->mapToGlobal(pos));
            return;
        }
        const QPoint global = m_tree->viewport()->mapToGlobal(pos);
        const QVariant sid = item->data(0, kRoleSourceId);
        if (sid.isValid() && item->data(0, kRoleNodeKind).toString() == QStringLiteral("source"))
            showSourceMenu(sid.toInt(), global);
        else if (item->data(0, kRoleDatasetId).isValid()
                 && item->data(0, kRoleTable).isValid()
                 && item->data(0, kRoleColumn).isValid())
            showColumnMenu(item, global);
    });

    connect(&m_repo, &DataRepository::datasetAdded, this, [this](DatasetId) { refresh(); });
    connect(&m_repo, &DataRepository::datasetRemoved, this, [this](DatasetId) { refresh(); });
    connect(&m_sources, &SourceRegistry::sourceAdded, this, [this](int) { refresh(); });
    connect(&m_sources, &SourceRegistry::sourceRemoved, this, [this](int) { refresh(); });
    connect(&m_sources, &SourceRegistry::sourceChanged, this, [this](int) { rebuildTree(); });

    registerPropertyProviders();
    refresh();
}

// 单击树节点 -> 统一选中对象。把稳定标识填进 ids，供属性 provider 反查。
void DataManagerPanel::reportSelection(QTreeWidgetItem *item)
{
    if (!item)
        return;
    const QString kind = item->data(0, kRoleNodeKind).toString();
    Selection sel;
    sel.ids.insert(QStringLiteral("source"), item->data(0, kRoleSourceId));
    sel.ids.insert(QStringLiteral("dataset"), item->data(0, kRoleDatasetId));
    sel.ids.insert(QStringLiteral("table"), item->data(0, kRoleTable));
    sel.ids.insert(QStringLiteral("column"), item->data(0, kRoleColumn));
    sel.label = item->text(0);

    if (kind == QStringLiteral("source"))
    {
        sel.kind = SelectionKind::Source;
        const int sid = item->data(0, kRoleSourceId).toInt();
        sel.ids.insert(QStringLiteral("sourceId"), sid);
    }
    else if (kind == QStringLiteral("dataset"))
        sel.kind = SelectionKind::Dataset;
    else if (kind == QStringLiteral("table"))
        sel.kind = SelectionKind::Table;
    else if (kind == QStringLiteral("column"))
        sel.kind = SelectionKind::Column;
    else if (kind == QStringLiteral("param"))
        sel.kind = SelectionKind::Param;
    else if (kind == QStringLiteral("msg"))
        sel.kind = SelectionKind::Message;
    else
        return; // 未知节点不上报
    m_appCtx.setSelection(sel);
}

// 注册数据源树各节点的属性提供者与写入器。
// provider 只查仓库生成 schema，不含任何 QWidget；setter 目前对数据源节点
// 主要是只读（写入留 placeholder，返回 false -> 面板走 prop.set 意图兜底）。
void DataManagerPanel::registerPropertyProviders()
{
    auto row = [](const QString &key, const QString &label, const QString &value,
                  PropKind kind = PropKind::ReadOnly) {
        PropRow r;
        r.key = key;
        r.label = label;
        r.value = value;
        r.kind = kind;
        r.editable = false;
        return r;
    };

    // --- 数据源 ---
    m_appCtx.interactions().registerPropertyProvider(SelectionKind::Source, [this, row](const Selection &sel) {
        const int sid = sel.ids.value(QStringLiteral("sourceId"),
                                      sel.ids.value(QStringLiteral("source"))).toInt();
        const DataSourceInfo *info = m_sources.get(sid);
        PropertySchema s;
        s.title = sel.label.isEmpty() ? QStringLiteral("数据源") : sel.label;
        PropObject o;
        o.id = QStringLiteral("source");
        o.label = QStringLiteral("数据源");
        if (info)
        {
            o.rows << row(QStringLiteral("name"), QStringLiteral("名称"), info->name);
            o.rows << row(QStringLiteral("origin"), QStringLiteral("来源路径"), info->origin);
            o.rows << row(QStringLiteral("format"), QStringLiteral("格式"), info->formatHint);
            o.rows << row(QStringLiteral("state"), QStringLiteral("状态"), stateText(info->state));
            o.rows << row(QStringLiteral("datasets"), QStringLiteral("数据集数"),
                          QString::number(info->datasetIds.size()));
        }
        else
            o.rows << row(QStringLiteral("name"), QStringLiteral("名称"), sel.label);
        s.objects << o;
        return s.toJson();
    });

    // --- 数据集 ---
    m_appCtx.interactions().registerPropertyProvider(SelectionKind::Dataset, [this, row](const Selection &sel) {
        const int dsId = sel.ids.value(QStringLiteral("dataset")).toInt();
        const Dataset *ds = m_repo.get(dsId);
        PropertySchema s;
        s.title = sel.label;
        PropObject o;
        o.id = QStringLiteral("dataset");
        o.label = QStringLiteral("数据集");
        o.rows << row(QStringLiteral("id"), QStringLiteral("ID"), QString::number(dsId));
        if (ds)
        {
            o.rows << row(QStringLiteral("name"), QStringLiteral("名称"), ds->name());
            o.rows << row(QStringLiteral("tables"), QStringLiteral("表数"), QString::number(ds->tableCount()));
        }
        s.objects << o;
        return s.toJson();
    });

    // --- 表 ---
    m_appCtx.interactions().registerPropertyProvider(SelectionKind::Table, [this, row](const Selection &sel) {
        const int dsId = sel.ids.value(QStringLiteral("dataset")).toInt();
        const QString tableName = sel.ids.value(QStringLiteral("table")).toString();
        const Dataset *ds = m_repo.get(dsId);
        const Table *t = ds ? ds->table(tableName) : nullptr;
        PropertySchema s;
        s.title = sel.label;
        PropObject o;
        o.id = QStringLiteral("table");
        o.label = QStringLiteral("表");
        o.rows << row(QStringLiteral("name"), QStringLiteral("表名"), tableName);
        if (t)
        {
            o.rows << row(QStringLiteral("rows"), QStringLiteral("行数"), QString::number(t->rows));
            o.rows << row(QStringLiteral("columns"), QStringLiteral("列数"), QString::number(t->columns.size()));
            o.rows << row(QStringLiteral("span"), QStringLiteral("时间跨度(s)"),
                          QString::number((t->lastTimeUs - t->firstTimeUs) / 1000000.0, 'f', 3));
        }
        s.objects << o;
        return s.toJson();
    });

    // --- 列（字段）：展示列元信息；可编辑"变换"行由曲线/衍生场景承接 ---
    m_appCtx.interactions().registerPropertyProvider(SelectionKind::Column, [this, row](const Selection &sel) {
        const int dsId = sel.ids.value(QStringLiteral("dataset")).toInt();
        const QString tableName = sel.ids.value(QStringLiteral("table")).toString();
        const QString columnName = sel.ids.value(QStringLiteral("column")).toString();
        const Dataset *ds = m_repo.get(dsId);
        const Table *t = ds ? ds->table(tableName) : nullptr;
        const Column *col = t ? t->column(columnName) : nullptr;
        PropertySchema s;
        s.title = sel.label;
        PropObject o;
        o.id = QStringLiteral("column");
        o.label = QStringLiteral("字段");
        o.rows << row(QStringLiteral("name"), QStringLiteral("列名"), columnName);
        o.rows << row(QStringLiteral("table"), QStringLiteral("所属表"), tableName);
        if (col)
        {
            o.rows << row(QStringLiteral("unit"), QStringLiteral("单位"), col->unit);
            o.rows << row(QStringLiteral("type"), QStringLiteral("类型"),
                          QString::number(static_cast<int>(col->type)));
            o.rows << row(QStringLiteral("rows"), QStringLiteral("行数"), QString::number(col->rowCount));
        }
        s.objects << o;
        PropObject hint;
        hint.id = QStringLiteral("actions");
        hint.label = QStringLiteral("操作");
        hint.rows << row(QStringLiteral("derive"), QStringLiteral("衍生"),
                         QStringLiteral("右键列 -> 添加变换（算子/表达式/Lua）"));
        s.objects << hint;
        return s.toJson();
    });

    // --- 参数 / 消息：占位只读（真实文本值待 Rust 文本读取槽，见架构文档）---
    auto kvProvider = [this, row](const Selection &sel) {
        PropertySchema s;
        s.title = sel.label;
        PropObject o;
        o.id = QStringLiteral("node");
        o.label = selectionKindName(sel.kind);
        o.rows << row(QStringLiteral("label"), QStringLiteral("名称"), sel.label);
        const int dsId = sel.ids.value(QStringLiteral("dataset")).toInt();
        o.rows << row(QStringLiteral("dataset"), QStringLiteral("数据集ID"), QString::number(dsId));
        s.objects << o;
        return s.toJson();
    };
    m_appCtx.interactions().registerPropertyProvider(SelectionKind::Param, kvProvider);
    m_appCtx.interactions().registerPropertyProvider(SelectionKind::Message, kvProvider);
}

void DataManagerPanel::selectSource(int sourceId)
{
    m_shownSourceId = sourceId;
    rebuildTree();
}

void DataManagerPanel::selectChip(int chip)
{
    if (chip == m_shownChip)
        return;
    m_shownChip = chip;
    rebuildTree();
}

void DataManagerPanel::rebuildChips()
{
    // 统计三类数量填充 chip 文本。
    int nFields = 0, nParams = 0, nMsgs = 0;
    m_paramCount = 0;
    m_msgCount = 0;
    for (int sid : m_sources.ids())
    {
        const DataSourceInfo *info = m_sources.get(sid);
        if (!info)
            continue;
        for (int dsId : info->datasetIds)
        {
            const Dataset *ds = m_repo.get(dsId);
            if (!ds)
                continue;
            for (int t = 0; t < ds->tableCount(); ++t)
            {
                const Table *tbl = ds->table(t);
                if (!tbl || tbl->rows <= 0)
                    continue;
                if (isKeyValueTable(tbl))
                {
                    if (tbl->name.contains(QStringLiteral("param"), Qt::CaseInsensitive) ||
                        tbl->name.contains(QStringLiteral("fact"), Qt::CaseInsensitive))
                    { nParams += tbl->rows; ++m_paramCount; }
                    else
                    { nMsgs += tbl->rows; ++m_msgCount; }
                }
                else
                    nFields += tbl->columns.size();
            }
        }
    }
    m_chips[0]->setText(QStringLiteral("字段 %1").arg(nFields));
    m_chips[1]->setText(QStringLiteral("参数 %1").arg(nParams));
    m_chips[2]->setText(QStringLiteral("消息 %1").arg(nMsgs));
    for (int i = 0; i < 3; ++i)
        m_chips[i]->setChecked(i == m_shownChip);
}

void DataManagerPanel::rebuildTree()
{
    const bool blocked = m_tree->blockSignals(true);
    m_tree->clear();
    rebuildChips();

    const bool showFields = m_shownChip == 0;
    m_header->setText(QStringLiteral("数据源 · %1 已连接").arg(m_sources.count()));

    for (int sid : m_sources.ids())
    {
        const DataSourceInfo *info = m_sources.get(sid);
        if (!info)
            continue;

        auto *srcItem = new QTreeWidgetItem(m_tree);
        srcItem->setText(0, info->name.isEmpty() ? QFileInfo(info->origin).fileName() : info->name);
        int chCount = 0;
        for (int dsId : info->datasetIds)
            if (const Dataset *ds = m_repo.get(dsId))
                for (int t = 0; t < ds->tableCount(); ++t)
                    if (!isKeyValueTable(ds->table(t)))
                        chCount += ds->table(t)->columns.size();
        srcItem->setText(1, QStringLiteral("%1 · %2 通道").arg(stateText(info->state)).arg(chCount));
        srcItem->setData(0, kRoleSourceId, sid);
        srcItem->setData(0, kRoleNodeKind, QStringLiteral("source"));
        srcItem->setData(0, Qt::UserRole, stateText(info->state)); // sourceStateText 用
        srcItem->setFirstColumnSpanned(false);

        if (showFields)
        {
            // 字段视图：dataset → 表 → 列。
            for (int dsId : info->datasetIds)
            {
                const Dataset *ds = m_repo.get(dsId);
                if (!ds)
                    continue;
                auto *dsItem = new QTreeWidgetItem(srcItem);
                dsItem->setText(0, QStringLiteral("#%1 %2").arg(dsId).arg(ds->name()));
                dsItem->setData(0, kRoleDatasetId, dsId);
                dsItem->setData(0, kRoleNodeKind, QStringLiteral("dataset"));

                for (int t = 0; t < ds->tableCount(); ++t)
                {
                    const Table *table = ds->table(t);
                    if (!table || isKeyValueTable(table))
                        continue;
                    auto *tItem = new QTreeWidgetItem(dsItem);
                    tItem->setText(0, table->name);
                    const double span = (table->lastTimeUs - table->firstTimeUs) / 1000000.0;
                    tItem->setText(1, QStringLiteral("%1 行 · %2 s").arg(table->rows).arg(span, 0, 'f', 2));
                    tItem->setData(0, kRoleTable, table->name);
                    tItem->setData(0, kRoleDatasetId, dsId);
                    tItem->setData(0, kRoleNodeKind, QStringLiteral("table"));

                    for (const Column &col : table->columns)
                    {
                        auto *cItem = new QTreeWidgetItem(tItem);
                        cItem->setText(0, col.name);
                        cItem->setText(1, col.unit);
                        cItem->setData(0, kRoleColumn, col.name);
                        cItem->setData(0, kRoleTable, table->name);
                        cItem->setData(0, kRoleDatasetId, dsId);
                        cItem->setData(0, kRoleNodeKind, QStringLiteral("column"));
                        cItem->setFlags(cItem->flags() | Qt::ItemIsDragEnabled);
                        cItem->setToolTip(0, QStringLiteral("拖到画布即可绘制 %1.%2").arg(table->name, col.name));
                    }
                    tItem->setExpanded(true);
                }
                dsItem->setExpanded(true);
            }
        }
        else if (m_shownChip == 1)
        {
            // 参数视图：每个 dataset 一个节点，下面是 param 表。
            for (int dsId : info->datasetIds)
            {
                const Dataset *ds = m_repo.get(dsId);
                if (!ds)
                    continue;
                for (int t = 0; t < ds->tableCount(); ++t)
                {
                    const Table *tbl = ds->table(t);
                    if (!tbl || !isKeyValueTable(tbl))
                        continue;
                    if (!(tbl->name.contains(QStringLiteral("param"), Qt::CaseInsensitive) ||
                          tbl->name.contains(QStringLiteral("fact"), Qt::CaseInsensitive)))
                        continue;
                    auto *pItem = new QTreeWidgetItem(srcItem);
                    pItem->setText(0, QStringLiteral("[%1] %2").arg(tbl->name).arg(
                        tbl->columns.isEmpty() ? QStringLiteral("—") : tbl->columns.first().name));
                    pItem->setText(1, QStringLiteral("%1 项").arg(tbl->rows));
                    pItem->setData(0, kRoleNodeKind, QStringLiteral("param"));
                    pItem->setData(0, kRoleDatasetId, dsId);
                    pItem->setFlags(pItem->flags() & ~Qt::ItemIsDragEnabled);
                }
            }
        }
        else
        {
            // 消息视图：每个非键值表一个消息节点。
            for (int dsId : info->datasetIds)
            {
                const Dataset *ds = m_repo.get(dsId);
                if (!ds)
                    continue;
                for (int t = 0; t < ds->tableCount(); ++t)
                {
                    const Table *tbl = ds->table(t);
                    if (!tbl || tbl->rows <= 0 || isKeyValueTable(tbl))
                        continue;
                    auto *mItem = new QTreeWidgetItem(srcItem);
                    mItem->setText(0, tbl->name);
                    mItem->setText(1, QStringLiteral("%1 字段 · %2 s").arg(tbl->columns.size())
                                          .arg(tbl->firstTimeUs / 1000000.0, 0, 'f', 2));
                    mItem->setData(0, kRoleNodeKind, QStringLiteral("msg"));
                    mItem->setData(0, kRoleDatasetId, dsId);
                    mItem->setFlags(mItem->flags() & ~Qt::ItemIsDragEnabled);
                }
            }
        }

        // 展开当前选中的源（老代码：同一时刻只展开一个源）。
        srcItem->setExpanded(sid == m_shownSourceId || m_shownSourceId < 0);
    }
    m_tree->blockSignals(blocked);
    applyFilter();
    updateEmptyState();
}

void DataManagerPanel::applyFilter()
{
    const QString needle = m_filter->text().trimmed();
    const bool empty = needle.isEmpty();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *src = m_tree->topLevelItem(i);
        bool srcHit = false;
        for (int d = 0; d < src->childCount(); ++d)
        {
            QTreeWidgetItem *child = src->child(d);
            bool childHit = empty || child->text(0).contains(needle, Qt::CaseInsensitive);
            for (int c = 0; c < child->childCount(); ++c)
            {
                QTreeWidgetItem *leaf = child->child(c);
                const bool hit = empty || childHit || leaf->text(0).contains(needle, Qt::CaseInsensitive);
                leaf->setHidden(!hit);
                childHit = childHit || hit;
            }
            child->setHidden(!childHit);
            srcHit = srcHit || childHit;
        }
        // 源名命中也算（展开全部子项）。
        const bool nameHit = empty || src->text(0).contains(needle, Qt::CaseInsensitive);
        src->setHidden(!(srcHit || nameHit));
    }
}

void DataManagerPanel::updateEmptyState()
{
    const bool hasSource = m_sources.count() > 0;
    m_tree->setVisible(hasSource);
    m_empty->setVisible(!hasSource);
}

void DataManagerPanel::showSourceMenu(int sourceId, const QPoint &globalPos)
{
    const DataSourceInfo *info = m_sources.get(sourceId);
    if (!info)
        return;
    QMenu menu(this);
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
        Intent intent; intent.op = QStringLiteral("data.reload");
        intent.params[QStringLiteral("id")] = sourceId;
        const auto r = m_executor.execute(intent);
        if (!r) QMessageBox::warning(this, QStringLiteral("重载失败"), r.error().message);
    }
    else if (chosen == exportCsv)
    {
        if (info->datasetIds.isEmpty())
        {
            QMessageBox::information(this, QStringLiteral("导出 CSV"), QStringLiteral("该数据源还没有解析出数据集"));
            return;
        }
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出 CSV"), QString(), QStringLiteral("CSV 文件 (*.csv)"));
        if (path.isEmpty()) return;
        Intent intent; intent.op = QStringLiteral("data.export");
        intent.params[QStringLiteral("dataset")] = info->datasetIds.first();
        intent.params[QStringLiteral("path")] = path;
        const auto r = m_executor.execute(intent);
        if (!r) QMessageBox::warning(this, QStringLiteral("导出失败"), r.error().message);
    }
    else if (chosen == clearCache)
    {
        Intent intent; intent.op = QStringLiteral("data.clearCache");
        const auto r = m_executor.execute(intent);
        if (!r) QMessageBox::warning(this, QStringLiteral("清除缓存失败"), r.error().message);
    }
    else if (chosen == activate)
        m_appCtx.setActiveSourceId(sourceId);
}

void DataManagerPanel::showColumnMenu(QTreeWidgetItem *item, const QPoint &globalPos)
{
    const QVariant dsId = item->data(0, kRoleDatasetId);
    const QVariant table = item->data(0, kRoleTable);
    const QVariant column = item->data(0, kRoleColumn);
    if (!dsId.isValid() || !table.isValid() || !column.isValid())
        return;
    QMenu menu(this);
    QAction *plot = menu.addAction(QStringLiteral("画到当前画布"));
    menu.addSeparator();
    QMenu *derive = menu.addMenu(QStringLiteral("添加变换…"));
    QList<QAction *> deriveActs;
    for (const DeriveRecipe &r : kDeriveRecipes)
    {
        QAction *a = derive->addAction(QString::fromUtf8(r.label));
        a->setData(QString::fromUtf8(r.id));
        deriveActs << a;
    }
    derive->addSeparator();
    QAction *openEditor = derive->addAction(QStringLiteral("变换编辑器（算子/表达式/Lua）…"));
    openEditor->setToolTip(QStringLiteral("按能力过滤的变换选择器；支持单点/批量在线调试"));
    QAction *chosen = menu.exec(globalPos);
    if (!chosen)
        return;

    const int datasetId = dsId.toInt();
    const QString tableName = table.toString();
    const QString columnName = column.toString();
    if (chosen == plot)
    {
        const int sourceId = m_sources.sourceOfDataset(datasetId);
        if (sourceId >= 0) m_appCtx.setActiveSourceId(sourceId);
        m_appCtx.setActiveChannel(datasetId, tableName, columnName);
        emit channelActivated(datasetId, tableName, columnName);
        return;
    }
    if (chosen == openEditor)
    {
        // 统一变换编辑器：按 CapDerived 过滤，配置算子/表达式/Lua 后走 data.derive。
        const QJsonObject req = TransformEditorDialog::run(
            this, m_appCtx, CapDerived,
            QStringLiteral("数据源衍生 · %1.%2").arg(tableName, columnName));
        if (req.isEmpty())
            return;
        Intent intent;
        intent.op = QStringLiteral("data.derive");
        intent.params[QStringLiteral("dataset")] = datasetId;
        intent.params[QStringLiteral("table")] = tableName;
        intent.params[QStringLiteral("column")] = columnName;
        intent.params[QStringLiteral("transform")] = QVariant(req);
        const QString label = TransformRegistry::get(
            req.value(QStringLiteral("descriptorId")).toString()).label;
        intent.params[QStringLiteral("name")] = tableName + QLatin1Char('.') + columnName
                                               + QStringLiteral(".") + label;
        const auto r = m_executor.execute(intent);
        if (!r)
            QMessageBox::warning(this, QStringLiteral("变换失败"), r.error().message);
        else
        {
            const int count = r.value().payload.toMap().value(QStringLiteral("count")).toInt();
            QMessageBox::information(this, QStringLiteral("衍生完成"),
                QStringLiteral("变换「%1」已算出 %2 点并叠加到画布，原数据未改动。")
                    .arg(label).arg(count));
        }
        return;
    }
    for (QAction *a : deriveActs)
    {
        if (chosen != a) continue;
        Intent intent; intent.op = QStringLiteral("data.derive");
        intent.params[QStringLiteral("dataset")] = datasetId;
        intent.params[QStringLiteral("table")] = tableName;
        intent.params[QStringLiteral("column")] = columnName;
        intent.params[QStringLiteral("filter")] = a->data().toString();
        intent.params[QStringLiteral("params")] = QString();
        const auto r = m_executor.execute(intent);
        if (!r)
            QMessageBox::warning(this, QStringLiteral("派生失败"), r.error().message);
        else
        {
            const QVariantMap pm = r.value().payload.toMap();
            const QString name = pm.value(QStringLiteral("name")).toString();
            const int count = pm.value(QStringLiteral("count")).toInt();
            const bool truncated = pm.value(QStringLiteral("truncated")).toBool();
            QMessageBox::information(this, QStringLiteral("派生完成"),
                QStringLiteral("已将「%1」送到画布叠加显示（%2 点%3）。\n原始数据未改动。")
                    .arg(name).arg(count)
                    .arg(truncated ? QStringLiteral("，已截取前 %1 点").arg(count) : QString()));
        }
        break;
    }
}

void DataManagerPanel::showRecentFilesMenu(const QPoint &globalPos)
{
    Intent intent; intent.op = QStringLiteral("data.recent");
    const auto r = m_executor.execute(intent);
    const QStringList files = r ? r.value().payload.toStringList() : QStringList();
    if (files.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("最近文件"), QStringLiteral("暂无最近文件"));
        return;
    }
    QMenu menu(this);
    for (const QString &path : files)
    {
        QAction *a = menu.addAction(QFileInfo(path).fileName());
        a->setToolTip(path);
        a->setData(path);
    }
    QAction *chosen = menu.exec(globalPos);
    if (!chosen) return;
    Intent load; load.op = QStringLiteral("data.load");
    load.params[QStringLiteral("path")] = chosen->data().toString();
    const auto lr = m_executor.execute(load);
    if (!lr) QMessageBox::warning(this, QStringLiteral("打开失败"), lr.error().message);
}

void DataManagerPanel::openLogFile()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("打开飞行日志"), QString(),
        QStringLiteral("数据文件 (*.csv *.ulg *.log *.bin *.mcap *.tlog);;全部 (*)"));
    if (path.isEmpty()) return;
    Intent load; load.op = QStringLiteral("data.load");
    load.params[QStringLiteral("path")] = path;
    const auto r = m_executor.execute(load);
    if (!r) QMessageBox::warning(this, QStringLiteral("打开失败"), r.error().message);
}

void DataManagerPanel::refresh()
{
    // 记住当前展开的源，重建后恢复。
    if (m_shownSourceId < 0 && m_sources.count() > 0)
        m_shownSourceId = m_sources.ids().first();
    rebuildTree();
}

int DataManagerPanel::sourceNodeCount() const { return m_tree->topLevelItemCount(); }

int DataManagerPanel::datasetNodeCount() const
{
    // 当前展开源下的 dataset 节点数（取第一个源，兼容单源测试）。
    QTreeWidgetItem *src = m_tree->topLevelItem(0);
    if (!src) return 0;
    int n = 0;
    for (int i = 0; i < src->childCount(); ++i)
        if (src->child(i)->data(0, kRoleNodeKind).toString() == QStringLiteral("dataset")) ++n;
    return n;
}

int DataManagerPanel::tableNodeCount(int datasetIndex) const
{
    QTreeWidgetItem *src = m_tree->topLevelItem(0);
    if (!src) return 0;
    int d = 0;
    for (int i = 0; i < src->childCount(); ++i)
    {
        QTreeWidgetItem *child = src->child(i);
        if (child->data(0, kRoleNodeKind).toString() != QStringLiteral("dataset")) continue;
        if (d++ == datasetIndex) return child->childCount();
    }
    return 0;
}

QString DataManagerPanel::sourceStateText(int row) const
{
    QTreeWidgetItem *src = m_tree->topLevelItem(row);
    return src ? src->data(0, Qt::UserRole).toString() : QString();
}

int DataManagerPanel::activeChip() const { return m_shownChip; }

QString DataManagerPanel::chipText(int index) const
{
    return (index >= 0 && index < 3 && m_chips[index]) ? m_chips[index]->text() : QString();
}

int DataManagerPanel::paramRowCount() const { return m_paramCount; }
int DataManagerPanel::msgRowCount() const { return m_msgCount; }

QWidget *createDatasetTreePanel(AppContext &appCtx, IIntentExecutor &exec, QWidget *parent)
{
    return new DataManagerPanel(appCtx, exec, parent);
}

// 视图层自注册：器官只声明类型字符串 "dataset-tree"，外壳据此装配本面板。
NG_REGISTER_PANEL("dataset-tree", datasetTree, createDatasetTreePanel)

} // namespace nextpilot::plotscope::gui
