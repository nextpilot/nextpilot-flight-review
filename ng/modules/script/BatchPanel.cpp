#include "BatchPanel.h"

#include "AppContext.h"
#include "ErrorCodes.h"
#include "IIntentExecutor.h"
#include "InteractionRegistry.h"
#include "PanelFactoryRegistry.h"
#include "Result.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QColor>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

using namespace nextpilot::plotscope::foundation;

namespace nextpilot::plotscope::script
{

using context::AppContext;
using intent::IIntentExecutor;

namespace
{
// view action id（模块与面板共用；字符串在此处为唯一事实，模块只广播同名 id）
constexpr const char *kActProgress = "data.batch.progress";
constexpr const char *kActSummary = "data.batch.summary";
constexpr const char *kActPick = "data.batch.pick";
constexpr const char *kActCancel = "data.batch.cancel";

QString metricsToText(const QVariant &metrics)
{
    const QVariantMap m = metrics.toMap();
    QStringList parts;
    for (auto it = m.constBegin(); it != m.constEnd(); ++it)
        parts << QStringLiteral("%1=%2").arg(it.key(), it.value().toString());
    return parts.join(QLatin1String("  "));
}

// 工厂函数（NG_REGISTER_PANEL_T 要求具名函数指针）
QWidget *createBatchSummaryPanel(AppContext &app, IIntentExecutor &ex, QWidget *parent)
{
    return new BatchSummaryPanel(app, ex, parent);
}
} // namespace

BatchSummaryPanel::BatchSummaryPanel(AppContext &appCtx, IIntentExecutor &executor,
                                     QWidget *parent)
    : QWidget(parent), m_app(&appCtx)
{
    Q_UNUSED(executor);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(6);

    // 顶栏：进度条 + 状态文本 + 取消
    auto *top = new QHBoxLayout;
    m_bar = new QProgressBar(this);
    m_bar->setRange(0, 1);
    m_bar->setValue(0);
    m_bar->setFormat(QStringLiteral("%v / %m"));
    top->addWidget(m_bar, 1);
    m_status = new QLabel(QStringLiteral("选择目录后点 Ribbon「批量运行」开始。"), this);
    top->addWidget(m_status);
    m_cancelBtn = new QPushButton(QStringLiteral("取消"), this);
    m_cancelBtn->setEnabled(false);
    m_cancelBtn->setToolTip(QStringLiteral("当前文件跑完后停止（协作式取消）"));
    top->addWidget(m_cancelBtn);
    lay->addLayout(top);

    // 汇总表：文件 | 结果 | 阶段 | 原因 | 指标
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({QStringLiteral("文件"), QStringLiteral("结果"),
                             QStringLiteral("阶段"), QStringLiteral("原因"),
                             QStringLiteral("指标")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->setAlternatingRowColors(true);
    m_tree->setUniformRowHeights(true);
    lay->addWidget(m_tree, 1);

    // data.batch.progress：每文件 upsert 一行（重复 run 覆盖同文件行）
    QPointer<BatchSummaryPanel> guard(this);
    m_app->interactions().registerViewAction(
        QLatin1String(kActProgress), [guard](const QVariantMap &params) -> Result<QVariant> {
            if (guard)
                guard->applyProgress(params);
            return Result<QVariant>(QVariant(true));
        });
    // data.batch.summary：收尾（成败计数 + 取消标记），恢复取消按钮态
    m_app->interactions().registerViewAction(
        QLatin1String(kActSummary), [guard](const QVariantMap &params) -> Result<QVariant> {
            if (guard)
                guard->applySummary(params);
            return Result<QVariant>(QVariant(true));
        });
    // data.batch.pick：目录 + 脚本文件对话框。params 已带 dir+script 时直接透传
    // （脚本/测试免对话框驱动批量；Ribbon 入口无参 → 弹对话框）。
    m_app->interactions().registerViewAction(
        QLatin1String(kActPick),
        [guard](const QVariantMap &params) -> Result<QVariant> {
            if (!guard)
                return fail(err::Unsupported, QStringLiteral("批量面板已销毁"));
            if (params.contains(QStringLiteral("dir"))
                && params.contains(QStringLiteral("script")))
                return Result<QVariant>(QVariant(params));
            const QString dir = QFileDialog::getExistingDirectory(
                guard, QStringLiteral("选择批量数据目录"));
            if (dir.isEmpty())
                return Result<QVariant>(QVariant());
            const QString script = QFileDialog::getOpenFileName(
                guard, QStringLiteral("选择批量脚本"), dir,
                QStringLiteral("Lua 脚本 (*.lua *.luau);;所有文件 (*)"));
            if (script.isEmpty())
                return Result<QVariant>(QVariant());
            QVariantMap out;
            out.insert(QStringLiteral("dir"), dir);
            out.insert(QStringLiteral("script"), script);
            return Result<QVariant>(QVariant(out));
        });
    connect(m_cancelBtn, &QPushButton::clicked, this, &BatchSummaryPanel::onCancel);
}

void BatchSummaryPanel::onCancel()
{
    Result<QVariant> out{QVariant()};
    m_app->interactions().dispatchViewAction(QLatin1String(kActCancel), QVariantMap(), out);
    m_status->setText(QStringLiteral("取消请求已发出，当前文件跑完后停止…"));
}

void BatchSummaryPanel::applyProgress(const QVariantMap &p)
{
    const int total = p.value(QStringLiteral("total")).toInt();
    const int done = p.value(QStringLiteral("done")).toInt();
    m_bar->setMaximum(qMax(1, total));
    m_bar->setValue(done);
    m_cancelBtn->setEnabled(done < total);

    const QString file = p.value(QStringLiteral("file")).toString();
    QTreeWidgetItem *item = upsertRow(file);
    const bool ok = p.value(QStringLiteral("ok")).toBool();
    item->setText(0, QFileInfo(file).fileName());
    item->setData(0, Qt::UserRole, file);
    item->setText(1, ok ? QStringLiteral("✓ 成功") : QStringLiteral("✗ 失败"));
    item->setForeground(1, ok ? QColor(0x16, 0xa3, 0x4a) : QColor(0xd9, 0x30, 0x25));
    item->setText(2, p.value(QStringLiteral("stage")).toString());
    item->setText(3, p.value(QStringLiteral("error")).toString());
    item->setText(4, metricsToText(p.value(QStringLiteral("metrics"))));
    m_status->setText(QStringLiteral("运行中 %1 / %2：").arg(done).arg(total)
                      + QFileInfo(file).fileName());
    m_tree->scrollToItem(item);
}

void BatchSummaryPanel::applySummary(const QVariantMap &p)
{
    m_lastSummary = p;
    m_cancelBtn->setEnabled(false);
    if (p.contains(QStringLiteral("error")))
    {
        m_status->setText(QStringLiteral("批量失败: %1").arg(p.value(QStringLiteral("error")).toString()));
        return;
    }
    if (p.value(QStringLiteral("cancelled")).toBool())
        m_status->setText(QStringLiteral("已取消：完成 %1 / %2（%3 失败）")
                              .arg(p.value(QStringLiteral("ok")).toInt())
                              .arg(p.value(QStringLiteral("total")).toInt())
                              .arg(p.value(QStringLiteral("failed")).toInt()));
    else
        m_status->setText(QStringLiteral("完成：%1 成功 / %2 失败（共 %3）")
                              .arg(p.value(QStringLiteral("ok")).toInt())
                              .arg(p.value(QStringLiteral("failed")).toInt())
                              .arg(p.value(QStringLiteral("total")).toInt()));
}

QTreeWidgetItem *BatchSummaryPanel::upsertRow(const QString &file)
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *it = m_tree->topLevelItem(i);
        if (it->data(0, Qt::UserRole).toString() == file)
            return it;
    }
    return new QTreeWidgetItem(m_tree);
}

} // namespace nextpilot::plotscope::script

// 自注册中央区面板（标题即页签文本；与脚本编辑器/数据表格同机制，外壳零改动）
NG_REGISTER_PANEL_T("batch-summary", batchSummary,
                    nextpilot::plotscope::script::createBatchSummaryPanel, "批量汇总")
