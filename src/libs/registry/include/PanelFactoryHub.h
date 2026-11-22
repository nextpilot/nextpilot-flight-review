#pragma once

#include "IIntentExecutor.h"
#include "StaticAutoRegistry.h"

#include <QString>
#include <QStringList>

#include <functional>

class QWidget;

// AppContext 真身在 context，仅前向声明供工厂签名用。
namespace nextpilot::plotscope::context
{
class AppContext;
}

namespace nextpilot::plotscope::registry
{

// 部件工厂表：把"部件类型标识"映射到具体的 QWidget 构造过程。
//
// 这是 app 骨架与模块之间的解耦点：
//   模块只说"我需要一个 dataset-tree 面板"（requestPanel("dock.left", "dataset-tree")），
//   视图层自己注册实现。模块因此不需要依赖 Qt Widgets，
//   headless（CLI）构建保持纯净；新增模块自带界面也不用改 GUI 层。
//
// **谁注册**：视图层（views/<功能>/）在各自 TU 里自注册，
// 外壳与其它功能一律不参与。于是：
//   - 加一个新功能 = 新建 views/<新功能>/ 目录 + 一个注册文件；
//   - 外壳（MainWindow / GuiContext / ShellBinder）不需任何改动。
//
// **为什么这张表在 libs/registry（机制库）而不在 gui**：
//   它是**纯数据**（QHash<QString, 函数指针>），QWidget 只做前置声明，
//   实现本身不需要 Qt Widgets。放在机制库才能让
//   **扩展包的呈现层**（领域专属面板，如 CAN 报文浏览器）注册进来——
//   否则扩展要注册面板就得依赖整个 gui 层，依赖方向就反了。
//   详见 ARCHITECTURE.md §6「领域自带面板」。
//
// 工厂签名带 AppContext 与意图执行器：
//   - AppContext：数据与共享状态（活动通道）；
//   - 执行器：视图取数走意图层（与 CLI/脚本同一条路径）。
//
// 实现薄壳：存储/查重/先到者为准全部委托 StaticHub<PanelEntry>（机制唯一），
// 本壳只保留领域方法（create 带上下文构造、types 排序输出）。

// 一条面板登记：工厂 + 显示标题（原"标题旁表"已并入条目，两表合一）。
struct PanelEntry
{
    using Factory = std::function<QWidget *(nextpilot::plotscope::context::AppContext &appCtx,
                                            nextpilot::plotscope::intent::IIntentExecutor &executor,
                                            QWidget *parent)>;

    Factory fn;
    QString title; // 停靠/中央页签显示文本；可为空

    bool valid() const { return fn != nullptr; }
};

class PanelFactoryHub : public StaticHub<PanelEntry>
{
public:
    using Factory = PanelEntry::Factory;

    // 注册一种部件类型。重复注册以先到者为准。
    // title：面板显示标题（停靠页签/中央页签用），纯数据，随注册一次给定。
    static void registerPanel(const QString &type, Factory factory, const QString &title = QString())
    {
        insert(type, PanelEntry{std::move(factory), title});
    }

    // 按类型创建部件；未注册返回 nullptr。
    static QWidget *create(const QString &type, nextpilot::plotscope::context::AppContext &appCtx,
                           nextpilot::plotscope::intent::IIntentExecutor &executor, QWidget *parent)
    {
        const PanelEntry *e = find(type);
        return e ? e->fn(appCtx, executor, parent) : nullptr;
    }

    // 已注册部件类型的显示标题；未注册/未给标题返回空串。
    static QString title(const QString &type)
    {
        const PanelEntry *e = find(type);
        return e ? e->title : QString();
    }

    // 已注册的部件类型（诊断/测试用），字典序排序。
    static QStringList types()
    {
        QStringList out;
        for (const QString &key : keys())
            out.append(key);
        out.sort();
        return out;
    }
};

// 注册宏：视图层 TU 末尾一行。
//
// 用法（views/<功能>/XxxPanel.cpp 末尾）：
//   PS_REGISTER_PANEL(dataset-tree, datasetTree, createDatasetTreePanel)
//
// 参数：
//   TypeId —— 部件类型标识**字面量**（如 "dataset-tree"），模块用它声明需求；
//              调用时须带引号：PS_REGISTER_PANEL("dataset-tree", datasetTree, fn)；
//   VarName —— 内部变量名（合法 C++ 标识符，如 datasetTree），用于生成唯一静态变量；
//   Fn —— 该 TU 里的工厂函数：
//     QWidget *createDatasetTreePanel(AppContext &, IIntentExecutor &, QWidget *);
//
// 这样新增视图 = 新增一个目录与一个文件，外壳与其它视图不动。
#define PS_REGISTER_PANEL(TypeId, VarName, Fn)                                           \
    namespace                                                                            \
    {                                                                                    \
    const bool g_ps_panel_reg_##VarName = [] {                                           \
        ::nextpilot::plotscope::registry::PanelFactoryHub::registerPanel(                \
            QStringLiteral(TypeId), &Fn);                                                \
        return true;                                                                     \
    }();                                                                                 \
    }

// 带标题版本：标题作停靠/中央页签的显示文本（如"脚本编辑器"）。
#define PS_REGISTER_PANEL_T(TypeId, VarName, Fn, Title)                                  \
    namespace                                                                            \
    {                                                                                    \
    const bool g_ps_panel_reg_##VarName = [] {                                           \
        ::nextpilot::plotscope::registry::PanelFactoryHub::registerPanel(                \
            QStringLiteral(TypeId), &Fn, QStringLiteral(Title));                         \
        return true;                                                                     \
    }();                                                                                 \
    }

} // namespace nextpilot::plotscope::registry
