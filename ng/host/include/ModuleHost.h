#pragma once

#include "IModule.h"
#include "IIntentExecutor.h"
#include "JobRecorder.h"
#include "ModuleInfo.h"
#include "Result.h"
#include "ExtensionRegistry.h"
#include "UiSlotRegistry.h"
#include "DataLoadHandler.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace ng::host
{

using ModuleFactory = std::function<IModule *()>;

struct ModuleRegistration
{
    ModuleInfo info;
    ModuleFactory factory;
};

// 宿主的核心职责：按依赖顺序装载模块、统一驱动生命周期、执行意图。
class ModuleHost : public QObject, public ng::core::IIntentExecutor
{
    Q_OBJECT
public:
    explicit ModuleHost(ng::core::DataRepository &repo, QObject *parent = nullptr);
    ~ModuleHost() override;

    // 注册一个模块的工厂（编译期或动态发现都走这里）。
    void registerModule(ModuleInfo info, ModuleFactory factory);

    // 按依赖拓扑顺序激活；失败隔离，返回被跳过的模块及原因。
    QHash<QString, QString> activate(const QStringList &disabledIds = {},
                                     UiContext *ui = nullptr);

    // 卸载模块：delete 对象，Qt 父子树自动回收其创建的 UI 与连接。
    bool deactivate(const QString &moduleId);
    void deactivateAll();

    // 模块注册自己能处理的意图 op（由 ModuleContext 转发，归属到当前初始化的模块）。
    void registerIntentHandler(ng::core::IIntentHandler *handler);

    // 执行意图：分派给声明了对应能力的模块。（实现 IIntentExecutor）
    ng::core::Result<ng::core::IntentResult> execute(const ng::core::Intent &intent) override;

    // 录制器：非空时 ScriptFacade 会把成功执行的业务意图录进去。
    void setRecorder(ng::core::JobRecorder *recorder) { m_recorder = recorder; }
    ng::core::JobRecorder *recorder() const { return m_recorder; }

    bool isActive(const QString &moduleId) const;
    QStringList activeIds() const;
    QList<ModuleInfo> availableModules() const;

    // 工具箱注册表：内置知识包自注册 + 外部插件扫描都汇入这里。
    ng::core::ExtensionRegistry &extensions() { return m_registry; }
    const ng::core::ExtensionRegistry &extensions() const { return m_registry; }

    // UI 插槽登记表：器官/骨架声明插槽，工具箱的 UI 贡献填内容。
    // 每次访问都从当前注册表重新绑定一次，保证装卸知识包后立即可见。
    const ng::core::UiSlotRegistry &uiSlots();

    // 可变引用：GUI 层需要声明新插槽（新增模块自带界面入口时用）。
    ng::core::UiSlotRegistry &uiSlotsMutable();

private:
    struct Node
    {
        ModuleRegistration reg;
        IModule *instance = nullptr;
        bool active = false;
        QList<ng::core::IIntentHandler *> handlers; // 该模块注册的意图处理器，卸载时移除
    };

    // 拓扑排序，返回有序 id；有环或缺依赖时把原因写入 errors。
    QList<QString> resolveOrder(const QHash<QString, QStringList> &provides,
                                QHash<QString, QString> &errors) const;

    ng::core::DataRepository &m_repo;
    ng::core::ExtensionRegistry m_registry;       // 工具箱：知识包/贡献的登记处
    ng::core::UiSlotRegistry m_uiSlotRegistry;    // UI 插槽：器官声明、工具箱填充
    DataLoadHandler m_dataLoadHandler;            // 系统级 data.load 入口，不归属可卸载模块
    QHash<QString, Node> m_nodes;
    QStringList m_order;
    QHash<QString, ng::core::IIntentHandler *> m_handlers; // op -> handler
    QString m_initializing; // 当前正在 initialize 的模块 id，用于 handler 归属
    ng::core::JobRecorder *m_recorder = nullptr; // 可选录制器，不拥有
};

} // namespace ng::host
