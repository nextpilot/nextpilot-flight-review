#pragma once

// DataTablePanel：数据表格查看面板（M2.2 2.2.3 / 2.2.4）。
//
// 形态：中央区页签（canvas.center），**按需创建**——数据树右键"查看数据表格"
// 才占中央区，与画布平级互不挤占（与脚本编辑器同一模式）。
//
// 通道（视图协作零新机制，全走 InteractionRegistry viewAction）：
//   data.table.open  {datasetId, table}   模块层入口：按需创建本面板再转发 show
//   data.table.show  {datasetId, table}   绑定数据集表 → 表格同步刷新
//
// 列交互（右键菜单）：
//   添加为曲线  → setActiveChannel（与数据树"画到当前画布"同路径，画布自动追加）
//   列统计      → 全列读取 + kernel 统计（min/max/均值/σ），摘要显示在底部栏
//
// 依赖 ng_data_tablemodel（懒加载模型）+ ng_dataset（只读行区间接口），
// 全部 QtCore 级数据通路；百万行翻页 <100ms 由模型层保证。

#include "DataTableModel.h"
#include "IIntentExecutor.h"

#include <QWidget>

class QLabel;
class QTableView;

namespace nextpilot::plotscope::context
{
class AppContext;
}

namespace nextpilot::plotscope::data
{

class DataTablePanel : public QWidget
{
    Q_OBJECT
public:
    explicit DataTablePanel(nextpilot::plotscope::context::AppContext &appCtx,
                            nextpilot::plotscope::intent::IIntentExecutor &executor,
                            QWidget *parent = nullptr);

    // --- 诊断/测试用 ---
    QString shownTable() const { return m_tableName; }
    int shownDatasetId() const { return m_datasetId; }
    QString statsText() const;

private:
    void attachViewActions();
    // 列统计摘要（2.2.4）：columnIndex 为 schema 列序（-1 = 时间列，不统计）。
    void showColumnStats(int columnIndex);
    // "添加为曲线"（2.2.3）：columnIndex 为 schema 列序。
    void addColumnAsCurve(int columnIndex);

    nextpilot::plotscope::context::AppContext &m_app;
    nextpilot::plotscope::intent::IIntentExecutor &m_executor;
    DataTableModel *m_model = nullptr;
    QTableView *m_view = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_statsLabel = nullptr; // 底部统计摘要行

    int m_datasetId = -1;
    QString m_tableName;
};

} // namespace nextpilot::plotscope::data
