#pragma once

#include "IModule.h"
#include "IIntentExecutor.h"
#include "JobRecorder.h"
#include "ModuleInfo.h"
#include "Result.h"
#include "AppContext.h"
#include "UiSlotRegistry.h"
#include "DataLoadHandler.h"
#include "DataCommandsHandler.h"

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
    // 宿主持有应用上下文（进程级运行态的唯一入口）。
    // 它不再自己拥有 repo/settings/toolbox——那些归 AppContext，避免状态分散两处。
    explicit ModuleHost(ng::core::AppContext &appCtx, QObject *parent = nullptr);
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

    // 注册**系统级**意图处理器：不归属任何可卸载模块，用保留 key 直接入池。
    // 供宿主构造期之外的系统能力使用（如 GUI 装配期的主题管理）——
    // 与 @system/data.* 同类：模块装卸不影响它。host 只认 IIntentHandler 接口，
    // 不 include 具体实现头，故不引入 GUI 依赖。
    void registerSystemHandler(const QString &key, ng::core::IIntentHandler *handler);

    // 执行意图：分派给声明了对应能力的模块。（实现 IIntentExecutor）
    ng::core::Result<ng::core::IntentResult> execute(const ng::core::Intent &intent) override;

    // 录制器：非空时 ScriptFacade 会把成功执行的业务意图录进去。
    void setRecorder(ng::core::JobRecorder *recorder) { m_recorder = recorder; }
    ng::core::JobRecorder *recorder() const { return m_recorder; }

    bool isActive(const QString &moduleId) const;
    QStringList activeIds() const;
    QList<ModuleInfo> availableModules() const;

    // 应用上下文：进程级运行态（数据集/设置/工具箱/数据源/摄取管道）。
    ng::core::AppContext &app() { return m_appCtx; }
    const ng::core::AppContext &app() const { return m_appCtx; }

    // 便捷转发（内部使用；器官请通过 ModuleContext::app() 取）。
    ng::core::DataRepository &data() { return m_appCtx.data(); }
    ng::core::SettingsStore &settings() { return m_appCtx.settings(); }
    ng::core::ExtensionRegistry &extensions() { return m_appCtx.toolbox(); }

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

    ng::core::AppContext &m_appCtx;             // 进程级运行态：唯一持有者
    ng::core::UiSlotRegistry m_uiSlotRegistry;   // UI 插槽：器官声明、工具箱填充
    DataLoadHandler m_dataLoadHandler;           // 系统级 data.load 入口，不归属可卸载模块
    DataCommandsHandler m_dataCommandsHandler;   // 系统级 data.reload/export/recent/clearCache，与 load 归口
    QHash<QString, Node> m_nodes;
    QStringList m_order;
    QHash<QString, ng::core::IIntentHandler *> m_handlers; // op -> handler
    QString m_initializing; // 当前正在 initialize 的模块 id，用于 handler 归属
    ng::core::JobRecorder *m_recorder = nullptr; // 可选录制器，不拥有
};

} // namespace ng::host
