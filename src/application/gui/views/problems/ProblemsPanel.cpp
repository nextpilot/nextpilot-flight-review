#include "ProblemsPanel.h"

#include "AppContext.h"
#include "InteractionHub.h"
#include "MessageLog.h"
#include "PanelFactoryHub.h"
#include "Result.h"
#include "ThemeManager.h"

#include <QDebug>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace nextpilot::plotscope::gui
{

using nextpilot::plotscope::context::AppContext;
using nextpilot::plotscope::context::Message;
using nextpilot::plotscope::foundation::Result;

namespace
{

const QStringList &problemLevels()
{
    static const QStringList levels = {QStringLiteral("warn"), QStringLiteral("error")};
    return levels;
}

// "name:line" → (name, line)；解析不出就不跳转（宁可不跳，不要跳到错文件）。
bool splitLocator(const QString &locator, QString &name, int &line)
{
    const int colon = locator.lastIndexOf(QLatin1Char(':'));
    if (colon <= 0)
        return false;
    bool ok = false;
    line = locator.mid(colon + 1).toInt(&ok);
    if (!ok || line <= 0)
        return false;
    name = locator.left(colon);
    return !name.isEmpty();
}

QWidget *createProblemsPanel(AppContext &appCtx,
                             nextpilot::plotscope::intent::IIntentExecutor &,
                             QWidget *parent)
{
    return new ProblemsPanel(appCtx, parent);
}

} // namespace

ProblemsPanel::ProblemsPanel(AppContext &appCtx, QWidget *parent)
    : QWidget(parent), m_appCtx(appCtx)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(4);

    auto *bar = new QHBoxLayout();
    bar->setSpacing(6);
    m_summary = new QLabel(this);
    bar->addWidget(m_summary);
    bar->addStretch(1);
    auto *clearBtn = new QPushButton(QStringLiteral("清空"), this);
    connect(clearBtn, &QPushButton::clicked, this, &ProblemsPanel::clearProblems);
    bar->addWidget(clearBtn);
    layout->addLayout(bar);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({QStringLiteral("级别"), QStringLiteral("来源"),
                                        QStringLiteral("描述"), QStringLiteral("定位")});
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_table, 1);

    // 双击定位：与终端里点错误行同一个落点（script.workbench.open + goto），
    // 不新增跳转通道——两处入口共用一个意图，行为才不会分叉。
    connect(m_table, &QTableWidget::itemDoubleClicked, this,
            [this](QTableWidgetItem *item) {
                if (!item)
                    return;
                const QString locator =
                    m_table->item(item->row(), 3) ? m_table->item(item->row(), 3)->text() : QString();
                QString name;
                int line = 0;
                if (!splitLocator(locator, name, line))
                    return;
                Result<QVariant> out{QVariant{}};
                QVariantMap open;
                open.insert(QStringLiteral("name"), name);
                if (!m_appCtx.interactions().dispatchViewAction(
                        QStringLiteral("script.workbench.open"), open, out))
                {
                    qWarning() << "[problems] 打开脚本失败:" << out.error().message;
                    return;
                }
                QVariantMap go;
                go.insert(QStringLiteral("name"), name);
                go.insert(QStringLiteral("line"), line);
                Result<QVariant> out2{QVariant{}};
                (void)m_appCtx.interactions().dispatchViewAction(
                    QStringLiteral("script.workbench.goto"), go, out2);
            });

    connect(&m_appCtx, &AppContext::messageAppended, this,
            [this](const QString &level, const QString &source, const QString &text,
                   const QString &locator) {
                appendMessage(level, source, text, locator);
            });

    reloadFromLog();
}

void ProblemsPanel::reloadFromLog()
{
    m_table->setRowCount(0);
    for (const Message &m : m_appCtx.messages().all())
        appendMessage(m.level, m.source, m.text, m.locator);
}

void ProblemsPanel::appendMessage(const QString &level, const QString &source, const QString &text,
                                  const QString &locator)
{
    if (!problemLevels().contains(level))
        return; // 输出面板收 info/debug，问题只留 warn/error
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    auto *levelItem = new QTableWidgetItem(level);
    levelItem->setForeground(QBrush(ThemeManager::themeColor(
        level == QStringLiteral("error") ? QStringLiteral("stateError")
                                         : QStringLiteral("stateWarn"))));
    m_table->setItem(row, 0, levelItem);
    m_table->setItem(row, 1, new QTableWidgetItem(source));
    m_table->setItem(row, 2, new QTableWidgetItem(text));
    m_table->setItem(row, 3, new QTableWidgetItem(locator));
    m_summary->setText(QStringLiteral("%1 条").arg(m_table->rowCount()));
}

void ProblemsPanel::clearProblems()
{
    // 与输出面板同一份消息流：清空即全局清空，避免"输出清了、问题还挂着"的错位。
    m_appCtx.messages().clear();
    m_table->setRowCount(0);
    m_summary->setText(QStringLiteral("0 条"));
}

int ProblemsPanel::rowCount() const
{
    return m_table ? m_table->rowCount() : 0;
}

} // namespace nextpilot::plotscope::gui

PS_REGISTER_PANEL_TI("problems", problemsPanel,
                     ::nextpilot::plotscope::gui::createProblemsPanel, "问题", "problems")
