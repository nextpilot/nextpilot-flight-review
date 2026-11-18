#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>

class QWidget;

namespace ng::core
{

class AppContext;
class IIntentExecutor;

// 部件工厂表：把"部件类型标识"映射到具体的 QWidget 构造过程。
//
// 这是 app 骨架与器官之间的解耦点：
//   器官只说"我需要一个 dataset-tree 面板"（requestPanel("dock.left", "dataset-tree")），
//   视图层自己注册实现。器官因此不需要依赖 Qt Widgets，
//   headless（CLI）构建保持纯净；新增器官自带界面也不用改 GUI 层。
//
// **谁注册**：视图层（views/<功能>/）在各自 TU 里自注册，
// 外壳与其它功能一律不参与。于是：
//   - 加一个新功能 = 新建 views/<新功能>/ 目录 + 一个注册文件；
//   - 外壳（MainWindow / GuiContext / ShellBinder）不需任何改动。
//
// **为什么这张表在 core 而不在 gui**：
//   它是**纯数据**（QHash<QString, 函数指针>），QWidget 只做前置声明，
//   实现本身不需要 Qt Widgets。放在 core 才能让
//   **知识包的呈现层**（领域专属面板，如 CAN 报文浏览器）注册进来——
//   否则扩展要注册面板就得依赖整个 gui 层，依赖方向就反了。
//   详见 ARCHITECTURE.md §6「领域自带面板」。
//
// 工厂签名带 AppContext 与意图执行器：
//   - AppContext：数据与共享状态（活动通道）；
//   - 执行器：视图取数走意图层（与 CLI/脚本同一条路径）。
class PanelFactoryRegistry
{
public:
    using Factory = std::function<QWidget *(ng::core::AppContext &appCtx,
                                            ng::core::IIntentExecutor &executor,
                                            QWidget *parent)>;

    // 注册一种部件类型。重复注册以先到者为准。
    static void registerPanel(const QString &type, Factory factory);

    // 按类型创建部件；未注册返回 nullptr。
    static QWidget *create(const QString &type, ng::core::AppContext &appCtx,
                           ng::core::IIntentExecutor &executor, QWidget *parent);

    // 已注册的部件类型（诊断/测试用）。
    static QStringList types();
};

// 注册宏：视图层 TU 末尾一行。
//
// 用法（views/<功能>/XxxPanel.cpp 末尾）：
//   NG_REGISTER_PANEL(dataset-tree, datasetTree, createDatasetTreePanel)
//
// 参数：
//   TypeId —— 部件类型标识**字面量**（如 "dataset-tree"），器官用它声明需求；
//              调用时须带引号：NG_REGISTER_PANEL("dataset-tree", datasetTree, fn)；
//   VarName —— 内部变量名（合法 C++ 标识符，如 datasetTree），用于生成唯一静态变量；
//   Fn —— 该 TU 里的工厂函数：
//     QWidget *createDatasetTreePanel(AppContext &, IIntentExecutor &, QWidget *);
//
// 这样新增视图 = 新增一个目录与一个文件，外壳与其它视图不动。
#define NG_REGISTER_PANEL(TypeId, VarName, Fn)                                         \
    namespace {                                                                        \
    const bool g_ng_panel_reg_##VarName = [] {                                         \
        ::ng::core::PanelFactoryRegistry::registerPanel(                                 \
            QStringLiteral(TypeId), &Fn);                                              \
        return true;                                                                   \
    }();                                                                               \
    }

} // namespace ng::core
