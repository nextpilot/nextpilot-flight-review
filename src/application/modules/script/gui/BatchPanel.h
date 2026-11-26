#pragma once

#include <QLabel>
#include <QProgressBar>
#include <QTreeWidget>
#include <QWidget>
#include <QVariantMap>

class QPushButton;

namespace nextpilot::plotscope::context
{
class AppContext;
}

namespace nextpilot::plotscope::intent
{
class IIntentExecutor;
}

namespace nextpilot::plotscope::script
{

// ============================================================================
// batch-summary：批量处理汇总面板。
//
// 数据源是 view action 广播（模块发、面板收，headless 下自然静默）：
//   data.batch.progress {done,total,file,ok,stage,error,metrics}  每文件一行/刷新
//   data.batch.summary {total,ok,failed,cancelled,done[,error]}   收尾状态
//   data.batch.pick {}                                            目录+脚本对话框
// 取消经 data.batch.cancel 回模块（分片边界生效，协作式）。
// ============================================================================
class BatchSummaryPanel final : public QWidget
{
    Q_OBJECT
public:
    BatchSummaryPanel(context::AppContext &appCtx,
                      nextpilot::plotscope::intent::IIntentExecutor &executor,
                      QWidget *parent = nullptr);

    // 测试/演示钩子：当前行数与最近一次收尾汇总
    int resultRowCount() const { return m_tree->topLevelItemCount(); }
    QVariantMap lastSummary() const { return m_lastSummary; }

private slots:
    void onCancel();

private:
    void applyProgress(const QVariantMap &p);
    void applySummary(const QVariantMap &p);
    QTreeWidgetItem *upsertRow(const QString &file);

    context::AppContext *m_app = nullptr;
    QProgressBar *m_bar = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QTreeWidget *m_tree = nullptr;
    QVariantMap m_lastSummary;
};

// 工厂（A2c 归位）：ScriptViewFactories 装配时经 registerEditorProvider 登记
// （editor viewType script.batchSummary，owner "script"）；原自注册已删。
QWidget *createBatchSummaryPanel(context::AppContext &appCtx, nextpilot::plotscope::intent::IIntentExecutor &executor,
                                 QWidget *parent);

} // namespace nextpilot::plotscope::script
