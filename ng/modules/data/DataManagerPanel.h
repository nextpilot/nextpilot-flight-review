#pragma once

#include "IIntentExecutor.h"
#include "AppContext.h"
#include "DatasetRegistry.h"
#include "SourceRegistry.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

// 数据源管理面板：左侧停靠区的**数据源树**（对齐老 DataSourceDock 的形态）。
//
// 结构（一源一行；单表源展开直接是列；多表源才显示表节点分组）：
//   ┌ 标题：数据源 · N 已连接 ────────────────────┐
//   │ [字段][参数][消息]  ← 三 chip 切换树内容      │
//   │ [ 搜索字段 / 参数 / 消息 ]  ← 搜索框            │
//   ├ 数据源树（QTreeWidget，每行一个源，可展开）────┤
//   │  ▼ demo_flight.csv        已加载 · 6 通道     │ ← 顶层：数据源（一行）
//   │        att_roll[deg]                        │   单表：列直接挂源下（dataset/表节点都省）
//   │        att_pitch[deg]                       │
//   │  ▼ multi.csv              已加载 · N 通道     │ ← 多表源：表节点分组
//   │     ▼ table_a             ...               │
//   │           col_x                             │
//   └────────────────────────────────────────────────┘
//
// 关键交互：顶层"数据源"行可展开/折叠（同一时刻只展开一个，抄老代码）；
// 列节点可拖到画布（ChannelMime）、双击激活、右键派生/画到画布/删除派生；
// 数据源行右键有重载/导出/清缓存/设为活动/移除；chip 切换"字段/参数/消息"三视图。
// 所有命令走意图层，视图之间靠共享状态星形解耦。
namespace nextpilot::plotscope::gui
{

class DataManagerPanel : public QWidget
{
    Q_OBJECT
public:
    explicit DataManagerPanel(nextpilot::plotscope::context::AppContext &appCtx,
                              nextpilot::plotscope::intent::IIntentExecutor &executor, QWidget *parent = nullptr);

    void refresh();

    // --- 诊断/测试用（契约保持不变）---
    int sourceNodeCount() const;             // 顶层数据源行数
    int datasetNodeCount() const;            // 当前展开源的数据集数（数据模型）
    int tableNodeCount(int datasetIndex) const; // 首源的字段表数（数据模型，含被省掉的节点）
    int shownSourceId() const { return m_shownSourceId; }
    QString sourceStateText(int row) const;
    int activeChip() const;
    QString chipText(int index) const;
    int paramRowCount() const;               // 参数节点数
    int msgRowCount() const;                 // 消息节点数

signals:
    void channelActivated(int datasetId, const QString &table, const QString &column);

private:
    void registerPropertyProviders();
    void reportSelection(QTreeWidgetItem *item);

    void rebuildTree();
    void rebuildChips();
    void applyFilter();
    void updateEmptyState();
    void selectSource(int sourceId);
    void selectChip(int chip);

    void showSourceMenu(int sourceId, const QPoint &globalPos);
    void showColumnMenu(QTreeWidgetItem *item, const QPoint &globalPos);
    void showRecentFilesMenu(const QPoint &globalPos);
    void openLogFile();
    void deriveColumn(QTreeWidgetItem *item, const QPoint &globalPos); // Ctrl+T：为列添加派生变换
    void removeDerivedChannels(QTreeWidgetItem *item);                 // 删除该列的派生曲线

    nextpilot::plotscope::context::AppContext &m_appCtx;
    nextpilot::plotscope::dataset::DatasetRegistry &m_repo;
    nextpilot::plotscope::dataset::SourceRegistry &m_sources;
    nextpilot::plotscope::intent::IIntentExecutor &m_executor;

    QLabel *m_header = nullptr;
    QLabel *m_srcCount = nullptr;            // 标题右侧「N源 · N已连接」
    QToolButton *m_chips[3] = {nullptr, nullptr, nullptr};
    QLineEdit *m_filter = nullptr;
    QTreeWidget *m_tree = nullptr;           // 统一数据源树：源 → (表) → 列；单表省表节点
    QWidget *m_empty = nullptr;              // 空态容器（无源时显示，树隐藏）：提示 + 三按钮

    int m_shownSourceId = -1;   // 当前展开的源
    int m_shownChip = 0;        // 0 字段 / 1 参数 / 2 消息
    int m_paramCount = 0;       // 诊断用
    int m_msgCount = 0;
};

QWidget *createDatasetTreePanel(nextpilot::plotscope::context::AppContext &appCtx, nextpilot::plotscope::intent::IIntentExecutor &exec,
                                QWidget *parent);

} // namespace nextpilot::plotscope::gui
