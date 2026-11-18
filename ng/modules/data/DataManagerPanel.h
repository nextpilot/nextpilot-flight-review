#pragma once

#include "AppContext.h"
#include "DataRepository.h"
#include "DataSourceRepository.h"

#include <QWidget>

namespace ng::core
{
class IIntentExecutor;
} // namespace ng::core

class QButtonGroup;
class QLabel;
class QLineEdit;
class QStackedWidget;
class QTableWidget;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

// 数据源管理面板：左侧停靠区的**源卡片列表 + 三视图检查器**。
//
// 物理归属：与 DataModule 同目录——一个"数据"功能的器官（意图/事件）
// 与它的专属视图放在一起，方便作为一个完整单元开发。二者靠 CMake 拆成
// ng_module_data（headless，CLI 装载）与 ng_module_data_gui（GUI 装载）。
// 视图通过 NG_REGISTER_PANEL("dataset-tree") 自注册到 core 工厂表，
// 器官只发字符串 id、不 include 本头文件——物理聚合但逻辑仍解耦。
//
// ## 结构（对齐老仓库 DataSourceDock 的信息层，不抄它的实现）
//
//   ┌ 标题：数据源 · N 已连接 ────────────────┐
//   │ ● demo_flight.csv        6 通道        │  ← 源卡片（状态点/名称/元信息）
//   │ ● other.csv              运行中        │
//   ├ 字段 N | 参数 N | 消息 N  ← 三 chip ─────┤
//   │ [ 搜索字段 / 参数 / 消息 ]              │
//   ├ 检查器（QStackedWidget）────────────────┤
//   │  字段：数据集 → 表 → 列（可拖到画布）    │
//   │  参数：参数 / 值（文本键值表）           │
//   │  消息：消息 / 速率 / 字段 / 首时刻        │
//   └──────────────────────────────────────────┘
//
// 为什么分成"字段/参数/消息"三视图：这是老 dock 已经确立、截图也沿用
// 的领域信息分层——字段是数值通道（画图用），参数是键值配置，消息是
// 事件/话题流。三者的数据形态和消费者都不同，混成一棵树只会互相干扰。
// 之前 ng 版本擅自把它压成单一"数据集→表→列"树并声明"参数/消息不再
// 单列"，那是脱离需求的架构想象，本版纠正回来。
//
// 数据来源只有 ng 仓库（DataSourceRepository / DataRepository / AppContext），
// 不引入老仓库的任何类型。文本键值（params/facts）的真实值暂不经
// readWindow 读出（它只返回 double），本版展示表清单/列结构/行数/首时刻，
// 文本值留占位——只读展示骨架，不伪造数据。
//
// 交互：字段树的"列"节点可拖到画布（ChannelMime）或双击激活；
// 参数/消息表只读。所有命令仍走意图层，视图之间靠共享状态星形解耦。
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
    int sourceNodeCount() const;             // 源卡片数
    int datasetNodeCount() const;            // 字段视图里的数据集节点数
    int tableNodeCount(int datasetIndex) const;
    // 检查器当前展示的是哪个源（-1 = 无）。
    int shownSourceId() const { return m_shownSourceId; }
    // 源列表某行的状态文本（测试断言用）。
    QString sourceStateText(int row) const;
    // 当前 chip 索引（0 字段 / 1 参数 / 2 消息），测试断言用。
    int activeChip() const;
    // chip 文本（如 "字段 6"），测试断言用。
    QString chipText(int index) const;
    // 参数 / 消息视图的行数，测试断言用。
    int paramRowCount() const;
    int msgRowCount() const;

signals:
    // 用户在字段树双击某个通道时发出：数据集 id、表名、列名。
    void channelActivated(int datasetId, const QString &table, const QString &column);

private:
    void rebuildSourceCards();
    void rebuildChips();
    void rebuildFields();
    void rebuildParams();
    void rebuildMessages();
    void applyFilter();
    void updateEmptyState();
    // 选中某源：更新检查器。
    void selectSource(int sourceId);
    // chip 切换：更新检查器当前视图。
    void selectChip(int chip);

    // 源卡片的右键菜单：只放**有底层支撑**的动作，全部走意图层。
    void showSourceMenu(int sourceId, const QPoint &globalPos);
    // 字段树列节点的右键菜单：派生通道 / 画到画布。
    void showColumnMenu(QTreeWidgetItem *item, const QPoint &globalPos);
    // 空态"最近文件"：读 data.recent 并弹出可点菜单，选中走 data.load。
    void showRecentFilesMenu(const QPoint &globalPos);
    // 空态"打开日志"：走与主入口一致的 data.load 路径。
    void openLogFile();

    ng::core::AppContext &m_appCtx;
    ng::core::DataRepository &m_repo;
    ng::core::DataSourceRepository &m_sources;
    ng::core::IIntentExecutor &m_executor; // 发意图（源右键菜单）

    QVBoxLayout *m_cardsLay = nullptr;       // 源卡片容器
    QButtonGroup *m_sourceGroup = nullptr;   // 源卡片互斥组
    QButtonGroup *m_chipGroup = nullptr;     // 字段/参数/消息 chip 组
    QToolButton *m_chips[3] = {nullptr, nullptr, nullptr};
    QLabel *m_header = nullptr;              // "数据源 · N 已连接"
    QLineEdit *m_filter = nullptr;           // 过滤框

    QStackedWidget *m_stack = nullptr;       // 空态 / 字段 / 参数 / 消息
    QWidget *m_empty = nullptr;
    QLabel *m_emptyTitle = nullptr;
    QTreeWidget *m_fieldTree = nullptr;      // 字段：数据集 → 表 → 列
    QTableWidget *m_paramTable = nullptr;    // 参数：键值表
    QTableWidget *m_msgTable = nullptr;      // 消息：事件/话题

    int m_shownSourceId = -1;   // 检查器当前展示的源
    int m_shownChip = 0;        // 当前 chip：0 字段 / 1 参数 / 2 消息
};

// 视图层自注册：数据集管理面板的工厂函数。
// 外壳与其它功能都不认识它——器官只声明类型字符串 "dataset-tree"。
QWidget *createDatasetTreePanel(ng::core::AppContext &appCtx, ng::core::IIntentExecutor &exec,
                                QWidget *parent);

} // namespace ng::gui
