#include "DataManagerPanel.h"

#include "PanelFactoryRegistry.h"
#include "ThemeManager.h"
#include "ChannelMime.h"

#include "AppContext.h"
#include "Dataset.h"
#include "DataSource.h"
#include "IIntentExecutor.h"
#include "Intent.h"

#include <QAbstractItemView>
#include <QButtonGroup>
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
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>
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

// 检查器让"列"节点可拖拽。拖拽只发逻辑标识（ChannelMime），
// 不暴露 Dataset 内部结构，画布据此走 plot.channel 意图取数。
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
        // 只有"列"节点可拖（三样标识齐全）。
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

// 派生算子清单：只列 Rust ps-ops::filters 里真实存在的算子（§12.6 不造空按钮）。
// 每项 = (算子 id, 中文名, 默认参数 JSON)。
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

// 一张表是否"文本键值表"：参数/事实这类表的特征是至少有一个 Text 列，
// 且行数有限（配置，不是待画的时间序列）。用它把 param 视图和字段视图分开。
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

    // --- 标题：数据源 · N 已连接 ---
    m_header = new QLabel(QStringLiteral("数据源"), this);
    m_header->setObjectName(QStringLiteral("sectionTitle"));
    m_header->setContentsMargins(8, 6, 8, 4);
    lay->addWidget(m_header);

    // --- 上段：源卡片列表 ---
    auto *cardsHost = new QWidget(this);
    m_cardsLay = new QVBoxLayout(cardsHost);
    m_cardsLay->setContentsMargins(6, 0, 6, 4);
    m_cardsLay->setSpacing(2);
    m_cardsLay->setAlignment(Qt::AlignTop);
    m_sourceGroup = new QButtonGroup(cardsHost);
    m_sourceGroup->setExclusive(true);
    lay->addWidget(cardsHost);

    // --- 中段：字段/参数/消息 chip + 搜索框 ---
    auto *chipHost = new QWidget(this);
    auto *chipLay = new QHBoxLayout(chipHost);
    chipLay->setContentsMargins(6, 2, 6, 2);
    chipLay->setSpacing(4);
    m_chipGroup = new QButtonGroup(chipHost);
    m_chipGroup->setExclusive(true);
    const char *chipLabels[3] = {"字段 0", "参数 0", "消息 0"};
    // 圆角药丸 chip，用 palette 关键字取色，深浅两套主题自动适配（§12.5）。
    const QString chipQss = QStringLiteral(
        "QToolButton#sourceChip{background:palette(button);border:1px solid palette(mid);"
        "border-radius:9px;color:palette(button-text);font-size:11px;padding:2px 8px;}"
        "QToolButton#sourceChip:hover{border-color:palette(highlight);}"
        "QToolButton#sourceChip:checked{background:palette(highlight);color:palette(highlighted-text);"
        "border-color:palette(highlight);}");
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
        m_chipGroup->addButton(chip, i);
        chipLay->addWidget(chip, 1);
        m_chips[i] = chip;
    }
    lay->addWidget(chipHost);

    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(QStringLiteral("搜索字段 / 参数 / 消息"));
    m_filter->setClearButtonEnabled(true);
    m_filter->setObjectName(QStringLiteral("searchBox"));
    m_filter->setContentsMargins(6, 0, 6, 0);
    lay->addWidget(m_filter);

    // --- 下段：检查器（空态 / 字段树 / 参数表 / 消息表）---
    m_stack = new QStackedWidget(this);

    // 空态
    m_empty = new QWidget(m_stack);
    auto *emptyLay = new QVBoxLayout(m_empty);
    emptyLay->setContentsMargins(12, 20, 12, 20);
    emptyLay->addStretch(1);
    m_emptyTitle = new QLabel(QStringLiteral("还没有数据源"), m_empty);
    m_emptyTitle->setAlignment(Qt::AlignCenter);
    m_emptyTitle->setObjectName(QStringLiteral("textStrong"));
    auto *emptyHint = new QLabel(QStringLiteral("打开飞行日志，或连接一个数据源开始分析"), m_empty);
    emptyHint->setAlignment(Qt::AlignCenter);
    emptyHint->setWordWrap(true);
    emptyHint->setObjectName(QStringLiteral("textDim"));
    emptyLay->addWidget(m_emptyTitle);
    emptyLay->addWidget(emptyHint);
    emptyLay->addSpacing(6);
    auto *openBtn = new QPushButton(QStringLiteral("打开日志"), m_empty);
    auto *liveBtn = new QPushButton(QStringLiteral("连接实时源"), m_empty);
    auto *recentBtn = new QPushButton(QStringLiteral("最近文件"), m_empty);
    for (QPushButton *b : {openBtn, liveBtn, recentBtn})
    {
        b->setObjectName(QStringLiteral("emptyAction"));
        b->setCursor(Qt::PointingHandCursor);
        emptyLay->addWidget(b);
    }
    emptyLay->addStretch(1);
    m_stack->addWidget(m_empty);

    // 字段树
    m_fieldTree = new ChannelTreeWidget(m_stack);
    m_fieldTree->setHeaderLabels({QStringLiteral("数据集 / 表 / 列"), QStringLiteral("行数 / 时长")});
    m_fieldTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_fieldTree->setAlternatingRowColors(true);
    m_fieldTree->setDragEnabled(true);
    m_fieldTree->setDragDropMode(QAbstractItemView::DragOnly);
    m_fieldTree->setDefaultDropAction(Qt::CopyAction);
    m_fieldTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_stack->addWidget(m_fieldTree);

    // 参数表：参数 / 值
    m_paramTable = new QTableWidget(0, 2, m_stack);
    m_paramTable->setHorizontalHeaderLabels({QStringLiteral("参数"), QStringLiteral("值")});
    m_paramTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_paramTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_paramTable->setColumnWidth(1, 140);
    m_paramTable->verticalHeader()->setVisible(false);
    m_paramTable->setShowGrid(false);
    m_paramTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_paramTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_stack->addWidget(m_paramTable);

    // 消息表：消息 / 速率 / 字段 / 首时刻
    m_msgTable = new QTableWidget(0, 4, m_stack);
    m_msgTable->setHorizontalHeaderLabels(
        {QStringLiteral("消息"), QStringLiteral("速率"), QStringLiteral("字段"), QStringLiteral("首时刻")});
    m_msgTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_msgTable->verticalHeader()->setVisible(false);
    m_msgTable->setShowGrid(false);
    m_msgTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_msgTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_stack->addWidget(m_msgTable);

    lay->addWidget(m_stack, 1);

    // --- 交互 ---

    connect(m_sourceGroup, &QButtonGroup::idClicked, this, &DataManagerPanel::selectSource);
    connect(m_chipGroup, &QButtonGroup::idClicked, this, &DataManagerPanel::selectChip);
    connect(m_filter, &QLineEdit::textChanged, this, &DataManagerPanel::applyFilter);

    // 字段树：双击 = 激活通道（走共享状态，星形解耦）。
    connect(m_fieldTree, &QTreeWidget::itemDoubleClicked, this,
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
                const int sourceId = m_sources.sourceOfDataset(datasetId);
                if (sourceId >= 0)
                    m_appCtx.setActiveSourceId(sourceId);
                m_appCtx.setActiveChannel(datasetId, tableName, columnName);
                emit channelActivated(datasetId, tableName, columnName);
            });

    // 字段树右键：派生通道 / 画到画布。
    connect(m_fieldTree, &QTreeWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) {
                QTreeWidgetItem *item = m_fieldTree->itemAt(pos);
                if (item)
                    showColumnMenu(item, m_fieldTree->viewport()->mapToGlobal(pos));
            });

    // 数据变化 -> 重建。
    connect(&m_repo, &DataRepository::datasetAdded, this, [this](DatasetId) { refresh(); });
    connect(&m_repo, &DataRepository::datasetRemoved, this, [this](DatasetId) { refresh(); });
    connect(&m_sources, &DataSourceRepository::sourceAdded, this, [this](int) { refresh(); });
    connect(&m_sources, &DataSourceRepository::sourceRemoved, this, [this](int) { refresh(); });
    connect(&m_sources, &DataSourceRepository::sourceChanged, this, [this](int) {
        rebuildSourceCards();
    });

    // 空态按钮
    connect(openBtn, &QPushButton::clicked, this, &DataManagerPanel::openLogFile);
    connect(liveBtn, &QPushButton::clicked, this, [this, liveBtn] {
        QMenu menu(this);
        menu.addAction(QStringLiteral("实时源扩展待接入（只读骨架）"))->setEnabled(false);
        menu.exec(liveBtn->mapToGlobal(QPoint(0, liveBtn->height())));
    });
    connect(recentBtn, &QPushButton::clicked, this,
            [this, recentBtn] { showRecentFilesMenu(recentBtn->mapToGlobal(QPoint(0, recentBtn->height()))); });

    refresh();
}

void DataManagerPanel::selectSource(int sourceId)
{
    if (sourceId == m_shownSourceId)
        return;
    m_shownSourceId = sourceId;
    rebuildChips();
    rebuildFields();
    rebuildParams();
    rebuildMessages();
    updateEmptyState();
    applyFilter();
}

void DataManagerPanel::selectChip(int chip)
{
    if (chip == m_shownChip)
        return;
    m_shownChip = chip;
    updateEmptyState();
    applyFilter();
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
        Intent intent;
        intent.op = QStringLiteral("data.reload");
        intent.params[QStringLiteral("id")] = sourceId;
        const auto r = m_executor.execute(intent);
        if (!r)
            QMessageBox::warning(this, QStringLiteral("重载失败"), r.error().message);
    }
    else if (chosen == exportCsv)
    {
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
        Intent intent;
        intent.op = QStringLiteral("data.clearCache");
        const auto r = m_executor.execute(intent);
        if (!r)
            QMessageBox::warning(this, QStringLiteral("清除缓存失败"), r.error().message);
    }
    else if (chosen == activate)
    {
        m_appCtx.setActiveSourceId(sourceId);
    }
}

void DataManagerPanel::showColumnMenu(QTreeWidgetItem *item, const QPoint &globalPos)
{
    const QVariant dsId = item->data(0, kRoleDatasetId);
    const QVariant table = item->data(0, kRoleTable);
    const QVariant column = item->data(0, kRoleColumn);
    if (!dsId.isValid() || !table.isValid())
        return; // 只在表/列节点弹菜单

    QMenu menu(this);
    QAction *draw = menu.addAction(QStringLiteral("画到画布"));
    QAction *derive = nullptr;
    if (column.isValid())
        derive = menu.addAction(QStringLiteral("派生通道…"));

    QAction *chosen = menu.exec(globalPos);
    if (!chosen)
        return;

    if (chosen == draw)
    {
        QString colName = column.isValid() ? column.toString() : QString();
        if (colName.isEmpty())
        {
            const Dataset *ds = m_repo.get(dsId.toInt());
            const Table *t = ds ? ds->table(table.toString()) : nullptr;
            if (t && !t->columns.isEmpty())
                colName = t->columns.first().name;
        }
        if (colName.isEmpty())
            return;
        const int datasetId = dsId.toInt();
        const int sourceId = m_sources.sourceOfDataset(datasetId);
        if (sourceId >= 0)
            m_appCtx.setActiveSourceId(sourceId);
        m_appCtx.setActiveChannel(datasetId, table.toString(), colName);
        emit channelActivated(datasetId, table.toString(), colName);
    }
    else if (chosen == derive && column.isValid())
    {
        QStringList labels;
        for (const DeriveRecipe &r : kDeriveRecipes)
            labels << QString::fromLatin1(r.label);
        bool ok = false;
        const QString picked = QInputDialog::getItem(
            this, QStringLiteral("派生通道"), QStringLiteral("选择算子（作用于 %1）")
                .arg(column.toString()), labels, 0, false, &ok);
        if (!ok || picked.isEmpty())
            return;
        const DeriveRecipe *recipe = nullptr;
        for (const DeriveRecipe &r : kDeriveRecipes)
            if (QString::fromLatin1(r.label) == picked) { recipe = &r; break; }
        if (!recipe)
            return;

        Intent intent;
        intent.op = QStringLiteral("data.derive");
        intent.params[QStringLiteral("dataset")] = dsId.toInt();
        intent.params[QStringLiteral("table")] = table.toString();
        intent.params[QStringLiteral("column")] = column.toString();
        intent.params[QStringLiteral("filter")] = QString::fromLatin1(recipe->id);
        intent.params[QStringLiteral("params")] = QString::fromLatin1(recipe->params);
        const auto r = m_executor.execute(intent);
        if (!r)
            QMessageBox::warning(this, QStringLiteral("派生失败"), r.error().message);
        else
        {
            const QVariantMap pm = r.value().payload.toMap();
            const QString name = pm.value(QStringLiteral("name")).toString();
            const int count = pm.value(QStringLiteral("count")).toInt();
            const bool truncated = pm.value(QStringLiteral("truncated")).toBool();
            QMessageBox::information(
                this, QStringLiteral("派生完成"),
                QStringLiteral("已将「%1」送到画布叠加显示（%2 点%3）。\n原始数据未改动。")
                    .arg(name)
                    .arg(count)
                    .arg(truncated ? QStringLiteral("，已截取前 %1 点").arg(count)
                                   : QString()));
        }
    }
}

void DataManagerPanel::showRecentFilesMenu(const QPoint &globalPos)
{
    Intent intent;
    intent.op = QStringLiteral("data.recent");
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
    if (!chosen)
        return;
    Intent load;
    load.op = QStringLiteral("data.load");
    load.params[QStringLiteral("path")] = chosen->data().toString();
    const auto lr = m_executor.execute(load);
    if (!lr)
        QMessageBox::warning(this, QStringLiteral("打开失败"), lr.error().message);
}

void DataManagerPanel::openLogFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("打开飞行日志"), QString(),
        QStringLiteral("数据文件 (*.csv *.ulg *.log *.bin *.mcap *.tlog);;全部 (*)"));
    if (path.isEmpty())
        return;
    Intent load;
    load.op = QStringLiteral("data.load");
    load.params[QStringLiteral("path")] = path;
    const auto r = m_executor.execute(load);
    if (!r)
        QMessageBox::warning(this, QStringLiteral("打开失败"), r.error().message);
}

void DataManagerPanel::refresh()
{
    rebuildSourceCards();
    rebuildChips();
    rebuildFields();
    rebuildParams();
    rebuildMessages();
    updateEmptyState();
    applyFilter();
}

void DataManagerPanel::rebuildSourceCards()
{
    const QList<QAbstractButton *> old = m_sourceGroup->buttons();
    for (QAbstractButton *b : old)
    {
        m_sourceGroup->removeButton(b);
        m_cardsLay->removeWidget(b);
        delete b;
    }

    const int n = m_sources.count();
    m_header->setText(QStringLiteral("数据源 · %1 已连接").arg(n));

    const int prevSourceId = m_shownSourceId;
    int firstId = -1;
    for (int sourceId : m_sources.ids())
    {
        const DataSourceInfo *info = m_sources.get(sourceId);
        if (!info)
            continue;
        if (firstId < 0)
            firstId = sourceId;

        QString display = info->name;
        if (display.isEmpty())
        {
            display = QFileInfo(info->origin).fileName();
            if (display.isEmpty())
                display = QStringLiteral("(未命名)");
        }
        int totalChannels = 0;
        for (int dsId : info->datasetIds)
            if (const Dataset *ds = m_repo.get(dsId))
                for (int t = 0; t < ds->tableCount(); ++t)
                    if (!isKeyValueTable(ds->table(t))) // 键值表不算"通道"
                        totalChannels += ds->table(t)->columns.size();

        // 文件源解析完成后状态是 Closed，但对用户呈现应是"已加载"而非"已停止"——
        // 与老 dock/截图一致（文件不是被人为停止，是加载完成后的自然态）。
        const QString shownState = (info->kind == SourceKind::File &&
                                    info->state == SourceState::Closed && info->datasetIds.size() > 0)
                                       ? QStringLiteral("已加载")
                                       : stateText(info->state);

        auto *card = new QToolButton(m_cardsLay->parentWidget());
        card->setCheckable(true);
        card->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        card->setObjectName(QStringLiteral("sourceCard"));
        card->setCursor(Qt::PointingHandCursor);
        card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        card->setStyleSheet(QStringLiteral(
            "QToolButton#sourceCard{text-align:left;padding:4px 6px;border:1px solid transparent;"
            "border-radius:3px;background:palette(button);color:palette(button-text);}"
            "QToolButton#sourceCard:hover{border-color:palette(mid);}"
            "QToolButton#sourceCard:checked{background:palette(alternate-base);"
            "border-color:palette(highlight);}"));
        card->setText(QStringLiteral("  %1\n  %2 通道 · %3")
                          .arg(display)
                          .arg(totalChannels)
                          .arg(shownState));
        card->setProperty("sourceId", sourceId);
        card->setProperty("stateText", stateText(info->state));
        card->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(card, &QToolButton::customContextMenuRequested, this,
                [this, sourceId, card](const QPoint &pos) {
                    showSourceMenu(sourceId, card->mapToGlobal(pos));
                });

        QStringList meta;
        meta << QStringLiteral("来源: %1").arg(info->origin);
        meta << QStringLiteral("种类: %1").arg(kindText(info->kind));
        meta << QStringLiteral("状态: %1").arg(stateText(info->state));
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
        card->setToolTip(meta.join(QLatin1Char('\n')));

        m_sourceGroup->addButton(card, sourceId);
        m_cardsLay->addWidget(card);
    }

    // 恢复选中：优先原选中；没有则选第一个。
    int target = -1;
    if (prevSourceId >= 0 && m_sourceGroup->button(prevSourceId))
        target = prevSourceId;
    else if (firstId >= 0)
        target = firstId;
    if (target >= 0)
    {
        if (QAbstractButton *b = m_sourceGroup->button(target))
            b->setChecked(true);
        if (m_shownSourceId != target)
            m_shownSourceId = target;
    }
    else
    {
        m_shownSourceId = -1;
    }

    // 卡片刚重建，检查器内容需要跟着新选中源刷新。
    rebuildChips();
    rebuildFields();
    rebuildParams();
    rebuildMessages();
}

void DataManagerPanel::rebuildChips()
{
    int nFields = 0, nParams = 0, nMsgs = 0;
    if (m_shownSourceId >= 0)
    {
        if (const DataSourceInfo *info = m_sources.get(m_shownSourceId))
        {
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
                            nParams += tbl->rows;
                        else
                            nMsgs += tbl->rows;
                    }
                    else
                    {
                        nFields += tbl->columns.size();
                    }
                }
            }
        }
    }
    m_chips[0]->setText(QStringLiteral("字段 %1").arg(nFields));
    m_chips[1]->setText(QStringLiteral("参数 %1").arg(nParams));
    m_chips[2]->setText(QStringLiteral("消息 %1").arg(nMsgs));
}

void DataManagerPanel::rebuildFields()
{
    const bool blocked = m_fieldTree->blockSignals(true);
    m_fieldTree->clear();

    if (m_shownSourceId >= 0)
    {
        if (const DataSourceInfo *info = m_sources.get(m_shownSourceId))
        {
            for (int dsId : info->datasetIds)
            {
                const Dataset *ds = m_repo.get(dsId);
                if (!ds)
                    continue;

                auto *dsItem = new QTreeWidgetItem(m_fieldTree);
                dsItem->setText(0, QStringLiteral("#%1 %2").arg(dsId).arg(ds->name()));
                dsItem->setData(0, kRoleDatasetId, dsId);
                dsItem->setFirstColumnSpanned(true);

                for (int t = 0; t < ds->tableCount(); ++t)
                {
                    const Table *table = ds->table(t);
                    if (!table || isKeyValueTable(table))
                        continue; // 键值表不进字段树（去参数/消息 tab）
                    auto *tItem = new QTreeWidgetItem(dsItem);
                    tItem->setText(0, table->name);
                    const double span = (table->lastTimeUs - table->firstTimeUs) / 1000000.0;
                    tItem->setText(1, QStringLiteral("%1 行 · %2 s")
                                          .arg(table->rows)
                                          .arg(span, 0, 'f', 2));
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
                        cItem->setFlags(cItem->flags() | Qt::ItemIsDragEnabled);
                        cItem->setToolTip(0, QStringLiteral("拖到画布即可绘制 %1.%2")
                                                  .arg(table->name, col.name));
                    }
                    tItem->setExpanded(true);
                }
                dsItem->setExpanded(true);
            }
        }
    }
    m_fieldTree->blockSignals(blocked);
}

void DataManagerPanel::rebuildParams()
{
    m_paramTable->setRowCount(0);
    if (m_shownSourceId < 0)
        return;
    const DataSourceInfo *info = m_sources.get(m_shownSourceId);
    if (!info)
        return;

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
            // 只读骨架：文本值暂不经 readWindow 读出（它只返回 double）。
            // 展示真实的表名/键列/值列/行数，值列留占位，不伪造数据。
            const QString cols = tbl->columns.isEmpty()
                                     ? QStringLiteral("—")
                                     : QStringLiteral("%1 → %2")
                                           .arg(tbl->columns.first().name)
                                           .arg(tbl->columns.last().name);
            const int row = m_paramTable->rowCount();
            m_paramTable->insertRow(row);
            m_paramTable->setItem(row, 0, new QTableWidgetItem(
                QStringLiteral("[%1] %2").arg(tbl->name, cols)));
            auto *v = new QTableWidgetItem(QStringLiteral("%1 项 · 文本值未接入").arg(tbl->rows));
            v->setForeground(ThemeManager::themeColor(QStringLiteral("textDim")));
            m_paramTable->setItem(row, 1, v);
        }
    }
}

void DataManagerPanel::rebuildMessages()
{
    m_msgTable->setRowCount(0);
    if (m_shownSourceId < 0)
        return;
    const DataSourceInfo *info = m_sources.get(m_shownSourceId);
    if (!info)
        return;

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
            const double firstSec = tbl->firstTimeUs / 1000000.0;
            const int row = m_msgTable->rowCount();
            m_msgTable->insertRow(row);
            m_msgTable->setItem(row, 0, new QTableWidgetItem(tbl->name));
            // 速率骨架：有首末时间才算得出，留占位；字段数与首时刻是真实元数据。
            auto *rate = new QTableWidgetItem(QStringLiteral("—"));
            rate->setForeground(ThemeManager::themeColor(QStringLiteral("textDim")));
            m_msgTable->setItem(row, 1, rate);
            m_msgTable->setItem(row, 2, new QTableWidgetItem(QString::number(tbl->columns.size())));
            m_msgTable->setItem(row, 3, new QTableWidgetItem(
                QStringLiteral("%1 s").arg(firstSec, 0, 'f', 2)));
        }
    }
}

void DataManagerPanel::applyFilter()
{
    const QString needle = m_filter->text().trimmed();
    const bool empty = needle.isEmpty();

    // 字段树：按表名/列名过滤（保留命中的祖先链）。
    for (int i = 0; i < m_fieldTree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *dsItem = m_fieldTree->topLevelItem(i);
        bool dsVisible = false;
        for (int t = 0; t < dsItem->childCount(); ++t)
        {
            QTreeWidgetItem *tItem = dsItem->child(t);
            const bool tableHit = empty || tItem->text(0).contains(needle, Qt::CaseInsensitive);
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

    // 参数/消息表：按行文本过滤。
    for (QTableWidget *tbl : {m_paramTable, m_msgTable})
    {
        for (int r = 0; r < tbl->rowCount(); ++r)
        {
            bool hit = empty;
            for (int c = 0; c < tbl->columnCount() && !hit; ++c)
            {
                QTableWidgetItem *it = tbl->item(r, c);
                if (it && it->text().contains(needle, Qt::CaseInsensitive))
                    hit = true;
            }
            tbl->setRowHidden(r, !hit);
        }
    }
}

void DataManagerPanel::updateEmptyState()
{
    const bool hasSource = m_sources.count() > 0 && m_shownSourceId >= 0;
    if (!hasSource)
    {
        m_stack->setCurrentWidget(m_empty);
        m_emptyTitle->setText(m_sources.count() == 0
                                  ? QStringLiteral("还没有数据源")
                                  : QStringLiteral("请选择一个数据源"));
        return;
    }
    // 三视图：0 字段 / 1 参数 / 2 消息；stack 里空态是 index 0，视图顺延 1。
    m_stack->setCurrentIndex(m_shownChip + 1);
}

int DataManagerPanel::sourceNodeCount() const
{
    return m_sourceGroup->buttons().size();
}

int DataManagerPanel::datasetNodeCount() const
{
    return m_fieldTree->topLevelItemCount();
}

int DataManagerPanel::tableNodeCount(int datasetIndex) const
{
    QTreeWidgetItem *item = m_fieldTree->topLevelItem(datasetIndex);
    return item ? item->childCount() : 0;
}

QString DataManagerPanel::sourceStateText(int row) const
{
    const QList<QAbstractButton *> btns = m_sourceGroup->buttons();
    if (row < 0 || row >= btns.size())
        return QString();
    return btns.at(row)->property("stateText").toString();
}

int DataManagerPanel::activeChip() const
{
    return m_shownChip;
}

QString DataManagerPanel::chipText(int index) const
{
    return (index >= 0 && index < 3 && m_chips[index]) ? m_chips[index]->text() : QString();
}

int DataManagerPanel::paramRowCount() const
{
    return m_paramTable->rowCount();
}

int DataManagerPanel::msgRowCount() const
{
    return m_msgTable->rowCount();
}

QWidget *createDatasetTreePanel(AppContext &appCtx, IIntentExecutor &exec, QWidget *parent)
{
    return new DataManagerPanel(appCtx, exec, parent);
}

// 视图层自注册：外壳与其它功能都不认识这个面板。
NG_REGISTER_PANEL("dataset-tree", datasetTree, createDatasetTreePanel)

} // namespace ng::gui
