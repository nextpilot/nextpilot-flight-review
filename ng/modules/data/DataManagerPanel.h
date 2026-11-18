#pragma once

#include "AppContext.h"
#include "DataRepository.h"

#include <QWidget>

namespace ng::core
{
class IIntentExecutor;
} // namespace ng::core

class QTreeWidget;
class QTreeWidgetItem;

// 数据集管理面板：左侧停靠区的树视图。
//
// 物理归属：与 DataModule 同目录——一个"数据"功能的器官（意图/事件）
// 与它的专属视图放在一起，方便作为一个完整单元开发。二者靠 CMake 拆成
// ng_module_data（headless，CLI 装载）与 ng_module_data_gui（GUI 装载）。
// 视图通过 NG_REGISTER_PANEL("dataset-tree") 自注册到 core 工厂表，
// 器官只发字符串 id、不 include 本头文件——物理聚合但逻辑仍解耦。
//
// 职责单一：展示"仓库里有哪些数据集、每张表有哪些列"，并把用户的
// 选列动作记入共享状态（AppContext 的活动通道）。
// 不认识格式、不做分析、不自己取数绘图。
//
// 数据来源是 DataRepository（唯一数据所有者）；面板只读元数据，
// 因此百万行数据集在树上同样只是一行摘要，不占内存。
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
    explicit DataManagerPanel(ng::core::AppContext &appCtx, QWidget *parent = nullptr);

    // 重建整棵树（仓库变化时调用）。
    void refresh();

    // 诊断/测试用：树里的数据集节点数、某数据集下的表节点数。
    int datasetNodeCount() const;
    int tableNodeCount(int datasetIndex) const;

signals:
    // 用户在树上双击某个通道时发出：数据集 id、表名、列名。
    void channelActivated(int datasetId, const QString &table, const QString &column);

private:
    void rebuild();

    ng::core::AppContext &m_appCtx;
    ng::core::DataRepository &m_repo;
    QTreeWidget *m_tree = nullptr;
};

// 视图层自注册：数据集管理面板的工厂函数。
// 外壳与其它功能都不认识它——器官只声明类型字符串 "dataset-tree"。
QWidget *createDatasetTreePanel(ng::core::AppContext &appCtx, ng::core::IIntentExecutor &exec,
                                QWidget *parent);

} // namespace ng::gui
