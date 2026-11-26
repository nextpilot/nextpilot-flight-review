#include "DataTablePanel.h"

#include "AppContext.h"
#include "Dataset.h"
#include "DatasetHub.h"
#include "ErrorCodes.h"
#include "InteractionHub.h"
#include "Result.h"

#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPoint>
#include <QTableView>
#include <QVBoxLayout>

#include <QPointer>

namespace nextpilot::plotscope::data
{

using namespace nextpilot::plotscope::foundation;
using nextpilot::plotscope::context::AppContext;
using nextpilot::plotscope::dataset::Dataset;
using nextpilot::plotscope::intent::IIntentExecutor;

namespace
{
// viewAction 通道 id（模块层入口在 DataModule::onAttach 注册 data.table.open）
constexpr auto kActShow = "data.table.show";
} // namespace

DataTablePanel::DataTablePanel(AppContext &appCtx, IIntentExecutor &executor, QWidget *parent)
    : QWidget(parent)
    , m_app(appCtx)
    , m_executor(executor)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);

    m_title = new QLabel(QStringLiteral("数据表格"), this);
    m_title->setStyleSheet(QStringLiteral("color: #666;"));
    lay->addWidget(m_title);

    m_model = new DataTableModel(this);
    m_view = new QTableView(this);
    m_view->setModel(m_model);
    m_view->setAlternatingRowColors(true);
    m_view->horizontalHeader()->setStretchLastSection(true);
    m_view->verticalHeader()->setDefaultSectionSize(22);
    // 大表禁用逐像素行高同步：verticalHeader 按索引定位即可（百万行零开销路径）。
    m_view->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
    m_view->setHorizontalScrollMode(QAbstractItemView::ScrollPerItem);
    lay->addWidget(m_view, 1);

    m_statsLabel = new QLabel(this);
    m_statsLabel->setStyleSheet(QStringLiteral("color: #2456a4;"));
    m_statsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(m_statsLabel);

    // 右键菜单：列统计 / 添加为曲线（列维度操作）。
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QTableView::customContextMenuRequested, this,
            [this](const QPoint &pos) {
                const QModelIndex idx = m_view->indexAt(pos);
                if (!idx.isValid())
                    return;
                if (m_datasetId < 0 || m_tableName.isEmpty())
                    return;
                QMenu menu(this);
                QAction *stats = nullptr;
                QAction *curve = nullptr;
                const int schemaCol = idx.column() - 1; // -1 = 时间列
                if (schemaCol >= 0)
                {
                    stats = menu.addAction(QStringLiteral("列统计"));
                    curve = menu.addAction(QStringLiteral("添加为曲线"));
                }
                QAction *chosen = menu.exec(m_view->viewport()->mapToGlobal(pos));
                if (!chosen)
                    return;
                if (chosen == stats)
                    showColumnStats(schemaCol);
                else if (chosen == curve)
                    addColumnAsCurve(schemaCol);
            });

    attachViewActions();
}

QString DataTablePanel::statsText() const
{
    return m_statsLabel ? m_statsLabel->text() : QString();
}

bool DataTablePanel::openDataset(int datasetId, const QString &table)
{
    const Dataset *ds = m_app.data().get(datasetId);
    if (!ds)
        return false;
    if (!ds->table(table))
        return false;

    m_datasetId = datasetId;
    m_tableName = table;
    m_model->setDataset(ds, table);
    m_statsLabel->clear();
    m_title->setText(QStringLiteral("数据表格 — #%1 / %2（%3 行）").arg(datasetId).arg(table).arg(m_model->rowCount()));
    return true;
}

void DataTablePanel::attachViewActions()
{
    nextpilot::plotscope::context::InteractionHub &reg = m_app.interactions();
    QPointer<DataTablePanel> guard(this);

    reg.registerViewAction(QLatin1String(kActShow),
                           [guard](const QVariantMap &params) -> Result<QVariant> {
        if (!guard)
            return fail(err::NoEnt, QStringLiteral("数据表格面板已关闭"));
        const int dsId = params.value(QStringLiteral("datasetId")).toInt();
        const QString table = params.value(QStringLiteral("table")).toString();
        if (!guard->openDataset(dsId, table))
        {
            const Dataset *ds = guard->m_app.data().get(dsId);
            if (!ds)
                return fail(err::NoEnt, QStringLiteral("数据集不存在: #%1").arg(dsId));
            return fail(err::NoEnt,
                        QStringLiteral("数据集 #%1 中不存在表: %2").arg(dsId).arg(table));
        }
        return Result<QVariant>(QVariant(true));
    });
}

void DataTablePanel::showColumnStats(int columnIndex)
{
    const Dataset *ds = m_app.data().get(m_datasetId);
    const auto *table = ds ? ds->table(m_tableName) : nullptr;
    if (!ds || !table || columnIndex < 0 || columnIndex >= table->columns.size())
        return;
    const QString colName = table->columns.at(columnIndex).name;
    // 全列读取（1M 行约 8MB）+ kernel 单遍统计；只读快照，零写回。
    QVector<double> values;
    if (ds->readColumnRows(m_tableName, colName, 0, table->rows, values) < 0)
    {
        m_statsLabel->setText(QStringLiteral("✘ 无法读取列: %1").arg(colName));
        return;
    }
    const auto stats = ds->stats(values, false);
    if (!stats)
    {
        m_statsLabel->setText(QStringLiteral("✘ 统计失败: %1").arg(colName));
        return;
    }
    m_statsLabel->setText(QStringLiteral("%1: N=%2  min=%3  max=%4  均值=%5  σ=%6")
                              .arg(colName)
                              .arg(stats->count)
                              .arg(stats->min, 0, 'g', 10)
                              .arg(stats->max, 0, 'g', 10)
                              .arg(stats->mean, 0, 'g', 10)
                              .arg(stats->stdDev, 0, 'g', 10));
}

void DataTablePanel::addColumnAsCurve(int columnIndex)
{
    const Dataset *ds = m_app.data().get(m_datasetId);
    const auto *table = ds ? ds->table(m_tableName) : nullptr;
    if (!ds || !table || columnIndex < 0 || columnIndex >= table->columns.size())
        return;
    // 与数据树"画到当前画布"同路径：活动通道变更 → 画布自动追加到当前 cell。
    m_app.setActiveChannel(m_datasetId, m_tableName, table->columns.at(columnIndex).name);
}

} // namespace nextpilot::plotscope::data

// 旧工厂表注册（PS_REGISTER_PANEL_TI "data.table"）已删（A2b 笔2b 第③步）：
// 本面板改由 DataViewFactories 经 ViewRegistry::registerEditorProvider 登记
// （viewType "data.table"、owner "data"，声明在 DataModule::initialize）。
