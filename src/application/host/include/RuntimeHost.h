#pragma once

#include "IWorkbenchContribution.h"
#include "IIntentExecutor.h"
#include "JobRecorder.h"
#include "ModuleInfo.h"
#include "Result.h"
#include "AppContext.h"
#include "InteractionHub.h"
#include "StaticRegistryNodes.h"
#include "UiSlotHub.h"
#include "DataLoadHandler.h"
#include "DataCommandsHandler.h"
#include "AIToolsHandler.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace nextpilot::plotscope::instantiation {
class InstantiationService; // 只前向声明：host 不 include 装配层服务容器头
}

namespace nextpilot::plotscope::host
{

using ModuleFactory = std::function<IWorkbenchContribution *()>;

struct ModuleRegistration
{
    ModuleInfo info;
    ModuleFactory factory;
};

// 宿主的核心职责：按依赖顺序装载模块、统一驱动生命周期、执行意图。
class RuntimeHost : public QObject, public nextpilot::plotscope::intent::IIntentExecutor
{
    Q_OBJECT
public:
    // 宿主持有应用上下文（进程级运行态的唯一入口）。
    // 它不再自己拥有 repo/settings/toolbox——那些归 AppContext，避免状态分散两处。
    explicit RuntimeHost(nextpilot::plotscope::context::AppContext &appCtx, QObject *parent = nullptr);
    ~RuntimeHost() override;

    // A1b §1.4.4：装配层在构造好服务容器后注入；activate 用它初始化模块。
    // 只存引用不拥有；只前向声明，host 不 include 装配层头。
    void setServices(nextpilot::plotscope::instantiation::InstantiationService &services) { m_services = &services; }

    // 注册一个模块的工厂（编译期或动态发现都走这里）。
    void registerModule(ModuleInfo info, ModuleFactory factory);

    // 按依赖拓扑顺序激活；失败隔离，返回被跳过的模块及原因。
    QHash<QString, QString> activate(const QStringList &disabledIds = {},
                                     IUiContext *ui = nullptr);

    // 卸载模块：delete 对象，Qt 父子树自动回收其创建的 UI 与连接。
    bool deactivate(const QString &moduleId);
    void deactivateAll();

    // 模块注册自己能处理的意图 op（归属到当前初始化的模块）。
    // 模块在 initialize(host) 里直接调用 host.registerIntentHandler(...)。
    void registerIntentHandler(nextpilot::plotscope::intent::IIntentHandler *handler);

    // 注册**系统级**意图处理器：不归属任何可卸载模块，用保留 key 直接入池。
    // 供宿主构造期之外的系统能力使用（如 GUI 装配期的主题管理）——
    // 与 @system/data.* 同类：模块装卸不影响它。host 只认 IIntentHandler 接口，
    // 不 include 具体实现头，故不引入 GUI 依赖。
    void registerSystemHandler(const QString &key, nextpilot::plotscope::intent::IIntentHandler *handler);

    // 执行意图：分派给声明了对应能力的模块。（实现 IIntentExecutor）
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> execute(const nextpilot::plotscope::intent::Intent &intent) override;

    // 录制器：非空时 ScriptFacade 会把成功执行的业务意图录进去。
    void setRecorder(nextpilot::plotscope::intent::JobRecorder *recorder) { m_recorder = recorder; }
    nextpilot::plotscope::intent::JobRecorder *recorder() const { return m_recorder; }

    bool isActive(const QString &moduleId) const;
    QStringList activeIds() const;
    // 已注册的意图 op 列表（供 ai.tools.list 暴露给 AI/MCP 工具层）。
    QStringList registeredOps() const { return m_handlers.keys(); }
    QList<ModuleInfo> availableModules() const;

    // 应用上下文：进程级运行态（数据集/设置/工具箱/数据源/摄取管道）。
    nextpilot::plotscope::context::AppContext &app() { return m_appCtx; }
    const nextpilot::plotscope::context::AppContext &app() const { return m_appCtx; }

    // 交互注册表：逻辑归 host 编排，实例由 AppContext owns。
    // module 在 initialize(host) 里经 host 到这里注册；GUI 经 app().interactions() 取用。
    nextpilot::plotscope::context::InteractionHub &interactions() { return m_appCtx.interactions(); }
    const nextpilot::plotscope::context::InteractionHub &interactions() const { return m_appCtx.interactions(); }
    // 派生序列注册表：独立派生字段（不写回 dataset），data.derive 登记、
    // data.series/plot.channel 按 name 穿透读数。实例归 AppContext。
    nextpilot::plotscope::context::DerivedSeriesHub &derivedHub() { return m_appCtx.derivedHub(); }
    const nextpilot::plotscope::context::DerivedSeriesHub &derivedHub() const { return m_appCtx.derivedHub(); }

    // 5 类扩展点的便捷转发：让"注册走 host()"成为统一入口（§7.9）。
    using ViewActionHandler = nextpilot::plotscope::context::InteractionHub::ViewActionHandler;
    using PropertyProvider = nextpilot::plotscope::context::InteractionHub::PropertyProvider;
    using PropertySetter = nextpilot::plotscope::context::InteractionHub::PropertySetter;
    using TransformEvalFn = nextpilot::plotscope::context::InteractionHub::TransformEvalFn;
    void registerViewAction(const QString &id, ViewActionHandler h)
    { m_appCtx.interactions().registerViewAction(id, std::move(h)); }
    bool dispatchViewAction(const QString &id, const QVariantMap &params,
                            nextpilot::plotscope::foundation::Result<QVariant> &out) const
    { return m_appCtx.interactions().dispatchViewAction(id, params, out); }
    void registerPropertyProvider(nextpilot::plotscope::uischema::SelectionKind kind, PropertyProvider p)
    { m_appCtx.interactions().registerPropertyProvider(kind, std::move(p)); }
    bool queryPropertySchema(const nextpilot::plotscope::uischema::Selection &sel, QJsonObject &out) const
    { return m_appCtx.interactions().queryPropertySchema(sel, out); }
    void registerPropertySetter(nextpilot::plotscope::uischema::SelectionKind kind, PropertySetter s)
    { m_appCtx.interactions().registerPropertySetter(kind, std::move(s)); }
    bool applyProperty(const nextpilot::plotscope::uischema::Selection &sel, const QString &objectId,
                       const QString &key, const QString &value, QString *err = nullptr) const
    { return m_appCtx.interactions().applyProperty(sel, objectId, key, value, err); }
    void registerTransformEvaluator(TransformEvalFn fn)
    { m_appCtx.interactions().registerTransformEvaluator(std::move(fn)); }
    bool evalTransform(const QJsonObject &request, uint32_t cap,
                       const QVector<double> &t, const QVector<double> &v,
                       nextpilot::plotscope::foundation::Result<QVector<double>> &out) const
    { return m_appCtx.interactions().evalTransform(request, cap, t, v, out); }
    void registerTransformScriptOps(nextpilot::plotscope::uischema::TransformScriptOps ops)
    { m_appCtx.interactions().registerTransformScriptOps(std::move(ops)); }
    void registerFilterOps(nextpilot::plotscope::uischema::FilterOps ops)
    { m_appCtx.interactions().registerFilterOps(std::move(ops)); }
    void registerMarkerOps(nextpilot::plotscope::uischema::MarkerOps ops)
    { m_appCtx.interactions().registerMarkerOps(std::move(ops)); }

    // 便捷转发（内部使用；模块请通过 host.app() 取）。
    nextpilot::plotscope::dataset::DatasetHub &data() { return m_appCtx.data(); }
    nextpilot::plotscope::context::SettingsStore &settings() { return m_appCtx.settings(); }
    nextpilot::plotscope::registry::ToolboxHub &toolbox() { return m_appCtx.toolbox(); }

    // UI 插槽登记表：模块/骨架声明插槽，工具箱的 UI 贡献填内容。
    // 每次访问都从当前注册表重新绑定一次，保证装卸扩展包后立即可见。
    const nextpilot::plotscope::registry::UiSlotHub &uiSlots();

    // 可变引用：GUI 层需要声明新插槽（新增模块自带界面入口时用）。
    nextpilot::plotscope::registry::UiSlotHub &uiSlotsMutable();

private:
    struct Node
    {
        ModuleRegistration reg;
        IWorkbenchContribution *instance = nullptr;
        bool active = false;
        QList<nextpilot::plotscope::intent::IIntentHandler *> handlers; // 该模块注册的意图处理器，卸载时移除
    };

    // M2.8 R4：static 注册表的节点宿主（生命周期归本宿主，树侧 QPointer 弱引用）。
    PanelFactoryNode *m_panelFactoryNode = nullptr;
    BuiltinModuleNode *m_builtinModuleNode = nullptr;
    // M2.8 遗留 4 收口：三张存量静态表全量上树（diag.registries 无盲区）。
    ParserNode *m_parserNode = nullptr;
    RecordingFormatNode *m_recordingFormatNode = nullptr;
    RecordingBackendNode *m_recordingBackendNode = nullptr;

    // 拓扑排序，返回有序 id；有环或缺依赖时把原因写入 errors。
    QList<QString> resolveOrder(const QHash<QString, QStringList> &provides,
                                QHash<QString, QString> &errors) const;

    nextpilot::plotscope::context::AppContext &m_appCtx;             // 进程级运行态：唯一持有者
    nextpilot::plotscope::instantiation::InstantiationService *m_services = nullptr; // 装配层注入的服务容器
    nextpilot::plotscope::registry::UiSlotHub m_uiSlotHub;   // UI 插槽：模块声明、工具箱填充（构造即上树）
    DataLoadHandler m_dataLoadHandler;           // 系统级 data.load 入口，不归属可卸载模块
    DataCommandsHandler m_dataCommandsHandler;   // 系统级 data.reload/export/recent/clearCache，与 load 归口
    AIToolsHandler m_aiToolsHandler;             // 系统级 ai.tools.list/ai.tool.call/ai.status
    QHash<QString, Node> m_nodes;
    QStringList m_order;
    QHash<QString, nextpilot::plotscope::intent::IIntentHandler *> m_handlers; // op -> handler
    QString m_initializing; // 当前正在 initialize 的模块 id，用于 handler 归属
    nextpilot::plotscope::intent::JobRecorder *m_recorder = nullptr; // 可选录制器，不拥有
};

} // namespace nextpilot::plotscope::host
