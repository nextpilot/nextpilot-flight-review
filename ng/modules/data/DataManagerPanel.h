#pragma once

#include "AppContext.h"
#include "DataRepository.h"
#include "DataSourceRepository.h"

#include <QWidget>

namespace ng::core
{
class IIntentExecutor;
} // namespace ng::core

class QLabel;
class QLineEdit;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;

// 数据源管理面板：左侧停靠区的**源列表 + 详情**两段式视图。
//
// 物理归属：与 DataModule 同目录——一个"数据"功能的器官（意图/事件）
// 与它的专属视图放在一起，方便作为一个完整单元开发。二者靠 CMake 拆成
// ng_module_data（headless，CLI 装载）与 ng_module_data_gui（GUI 装载）。
// 视图通过 NG_REGISTER_PANEL("dataset-tree") 自注册到 core 工厂表，
// 器官只发字符串 id、不 include 本头文件——物理聚合但逻辑仍解耦。
//
// ## 结构（对齐老代码 DataSourceDock 的信息层，不抄它的实现）
//
//   ┌ 上段：源列表 ────────────────┐
//   │ ● demo_flight.ulg  CSV 3.2MB │  ← 状态点 / 名称 / 元信息 / 进度
//   │ ● other.csv         已停止    │
//   └──────────────────────────────┘
//   ┌ 下段：选中源的详情 ───────────┐
//   │ ▾ #0 demo_flight              │  ← 数据集 → 表 → 列
//   │   ▾ trace                     │
//   │     roll   deg                │
//   └──────────────────────────────┘
//
// ## 与老 dock 的差异（都是新架构的必然结果，不是偷工）
//
//   · **源与数据集分层**：新架构里一个源可扇出多个解析器、产出多个数据集，
//     所以详情树是「数据集 → 表 → 列」；老 dock 只有「字段/参数/消息」三视图。
//   · **参数/消息不再是独立视图**：新架构的解析器统一产出**表**
//     （ULog 的 params/facts 已落库为普通表），无需为它们单开 tab。
//   · **没有 live 连接/变换派生/设备调试**：这些在 ng 尚无底层支撑，
//     不做点了没反应的按钮（老 dock 有）。
//
// 职责单一：展示"仓库里有哪些源、每个源有哪些表和列"，并把用户的
// 选列动作记入共享状态（AppContext 的活动通道）。
// 不认识格式、不做分析、不自己取数绘图、不直接写数据源仓库。
//
// 数据来源是 DataSourceRepository（源）与 DataRepository（数据集）；
// 面板只读元数据，因此百万行数据集在树上同样只是一行摘要，不占内存。
//
// 协作方式：双击某列 → 记入 AppContext 的"活动通道" →
// 订阅该状态的视图（如绘图视图）自行去取数渲染。
// 面板因此**不认识其它视图**，视图之间是星形而非网状。
namespace ng::gui
{

class DataManagerPanel : public QWidget
{
    Q_OBJECT
public:
    explicit DataManagerPanel(ng::core::AppContext &appCtx,
                              ng::core::IIntentExecutor &executor, QWidget *parent = nullptr);

    // 重建整个面板（源/数据集变化时调用）。
    void refresh();

    // --- 诊断/测试用 ---
    int sourceNodeCount() const;             // 源列表里的源数
    int datasetNodeCount() const;            // 详情树里的数据集节点数
    int tableNodeCount(int datasetIndex) const;
    // 详情树当前展示的是哪个源（-1 = 无）。
    int shownSourceId() const { return m_shownSourceId; }
    // 源列表某行的状态文本（测试断言用）。
    QString sourceStateText(int row) const;

signals:
    // 用户在树上双击某个通道时发出：数据集 id、表名、列名。
    void channelActivated(int datasetId, const QString &table, const QString &column);

private:
    void rebuildSources();
    void rebuildDetails();
    void applyFilter();
    void updateEmptyState();
    // 源行的右键菜单：只放**有底层支撑**的动作，全部走意图层。
    void showSourceMenu(QTreeWidgetItem *item, const QPoint &globalPos);

    ng::core::AppContext &m_appCtx;
    ng::core::DataRepository &m_repo;
    ng::core::DataSourceRepository &m_sources;
    ng::core::IIntentExecutor &m_executor; // 发意图（源右键菜单）

    QTreeWidget *m_sourceTree = nullptr; // 上段：源列表
    QLineEdit *m_filter = nullptr;       // 过滤框（源名 + 表列名）
    QTreeWidget *m_tree = nullptr;        // 下段：详情（数据集 → 表 → 列）
    QStackedWidget *m_stack = nullptr;   // 空态 / 详情
    QLabel *m_emptyTitle = nullptr;
    QLabel *m_emptyHint = nullptr;

    int m_shownSourceId = -1;   // 详情树当前展示的源
};

// 视图层自注册：数据集管理面板的工厂函数。
// 外壳与其它功能都不认识它——器官只声明类型字符串 "dataset-tree"。
QWidget *createDatasetTreePanel(ng::core::AppContext &appCtx, ng::core::IIntentExecutor &exec,
                                QWidget *parent);

} // namespace ng::gui
