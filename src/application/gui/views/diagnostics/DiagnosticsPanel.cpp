#include "DiagnosticsPanel.h"

#include "AppContext.h"
#include "InteractionHub.h"
#include "RegistryNode.h"
#include "RegistryTree.h"
#include "Result.h"

#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>

namespace nextpilot::plotscope::gui
{

using nextpilot::plotscope::context::AppContext;
using nextpilot::plotscope::foundation::Result;

// 视图工厂（导出，见 DiagnosticsPanel.h）：装配层（BuiltinWorkbenchViews）经它
// 注册进 ViewRegistry（registerViewProvider，owner core.workbench）；旧工厂表注册已删。
QWidget *createDiagnosticsPanel(AppContext &appCtx,
                                nextpilot::plotscope::intent::IIntentExecutor &executor,
                                QWidget *parent)
{
    return new DiagnosticsPanel(appCtx, executor, parent);
}

DiagnosticsPanel::DiagnosticsPanel(AppContext &appCtx,
                                   nextpilot::plotscope::intent::IIntentExecutor &executor,
                                   QWidget *parent)
    : QWidget(parent), m_appCtx(appCtx)
{
    Q_UNUSED(executor); // 取数走 interactions().dispatchViewAction（GUI/脚本同路径）

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({QStringLiteral("kind"), QStringLiteral("name"),
                                        QStringLiteral("count"), QStringLiteral("keys")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_table);

    auto *refreshBtn = new QPushButton(QStringLiteral("刷新"), this);
    connect(refreshBtn, &QPushButton::clicked, this, [this] { refresh(); });
    layout->addWidget(refreshBtn);

    refresh();
}

int DiagnosticsPanel::refresh()
{
    Result<QVariant> out{QVariant{}};
    if (!m_appCtx.interactions().dispatchViewAction(QStringLiteral("diag.registries"), {}, out)
        || !out.ok())
        return 0;

    const QVariantList rows = out.value().toList();
    m_table->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i)
    {
        const QVariantMap row = rows.at(i).toMap();
        const QString keys =
            row.value(QStringLiteral("keys")).toStringList().join(QStringLiteral(", "));
        m_table->setItem(i, 0, new QTableWidgetItem(
                                    row.value(QStringLiteral("kind")).toString()));
        m_table->setItem(i, 1, new QTableWidgetItem(
                                    row.value(QStringLiteral("name")).toString()));
        m_table->setItem(i, 2, new QTableWidgetItem(
                                    QString::number(
                                        row.value(QStringLiteral("count")).toInt())));
        m_table->setItem(i, 3, new QTableWidgetItem(keys));
    }
    return rows.size();
}

} // namespace nextpilot::plotscope::gui
