// CAN 报文浏览器：**汽车领域的专属面板**。
//
// 为什么它住在扩展的呈现层，而不是 gui/views/：
//   这个面板的形态**本身就是领域知识** —— 列有哪些信号、怎么分组、
//   数值怎么格式化，都取决于 DBC 定义的报文与信号。
//   gui/views/ 是 app 的**通用功能**（数据集树、绘图画布……），
//   让它长出 CAN 报文浏览器，就成了领域杂物箱。
//   而模块也给不出形态（模块是机制，不懂业务）。
//   于是形态归**扩展包**：描述在 AutomotivePack（纯数据，headless 可见），
//   实现在这里（GUI 侧）。
//
// 它与通用视图共用同一张工厂表（PanelFactoryHub，在 core），
// 所以模块调用 requestDomainPanel("dock.right", "car.can-browser") 就能拿到它，
// 不必知道它是领域面板还是通用面板。

#include "CanBrowserPanel.h"

#include "AppContext.h"
#include "IIntentExecutor.h"
#include "PanelFactoryHub.h"

#include <QHeaderView>
#include <QTableWidget>
#include <QVBoxLayout>

namespace nextpilot::plotscope::ext::automotive
{

CanBrowserPanel::CanBrowserPanel(nextpilot::plotscope::context::AppContext &appCtx, QWidget *parent)
    : QWidget(parent), m_appCtx(appCtx)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("名称"),
                                         QStringLiteral("DLC"), QStringLiteral("周期(ms)")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(m_table);

    // 内容随数据源动态变化：仓库里有数据集就列出它们可能涉及的报文。
    // 这里先给出骨架（空表），真实填充等 CAN 格式接入后由数据驱动。
    connect(&m_appCtx.data(), &nextpilot::plotscope::dataset::DatasetHub::datasetAdded, this,
            [this](nextpilot::plotscope::dataset::DatasetId) { refresh(); });
    connect(&m_appCtx.data(), &nextpilot::plotscope::dataset::DatasetHub::datasetRemoved, this,
            [this](nextpilot::plotscope::dataset::DatasetId) { refresh(); });
    refresh();
}

void CanBrowserPanel::refresh()
{
    // 数据源里若有 CAN 相关表，按其 schema 动态生成列与行——
    // 这正是"形态随业务变化"的落点：面板自己从数据推出形态，
    // 不需要 GUI 层或模块预先知道会有哪些信号。
    const auto datasets = m_appCtx.data().ids();
    m_table->setRowCount(0);
    for (nextpilot::plotscope::dataset::DatasetId id : datasets)
    {
        const nextpilot::plotscope::dataset::Dataset *ds = m_appCtx.data().get(id);
        if (!ds)
            continue;
        for (int ti = 0; ti < ds->tableCount(); ++ti)
        {
            const nextpilot::plotscope::dataset::Table *t = ds->table(ti);
            if (!t || !t->name.contains(QStringLiteral("can"), Qt::CaseInsensitive))
                continue;
            const int row = m_table->rowCount();
            m_table->insertRow(row);
            m_table->setItem(row, 0, new QTableWidgetItem(t->name));
            m_table->setItem(row, 1, new QTableWidgetItem(ds->name()));
            m_table->setItem(row, 2, new QTableWidgetItem(QString::number(t->rowCount())));
            m_table->setItem(row, 3, new QTableWidgetItem(QString()));
        }
    }
}

QWidget *createCanBrowserPanel(nextpilot::plotscope::context::AppContext &appCtx, nextpilot::plotscope::intent::IIntentExecutor &exec,
                               QWidget *parent)
{
    Q_UNUSED(exec); // 本面板自己取数，不经意图层
    return new CanBrowserPanel(appCtx, parent);
}

} // namespace nextpilot::plotscope::ext::automotive

// 注册进**同一张**工厂表：领域面板与通用面板共用，模块不区分。
PS_REGISTER_PANEL("car.can-browser", canBrowserPanel, ::nextpilot::plotscope::ext::automotive::createCanBrowserPanel)
