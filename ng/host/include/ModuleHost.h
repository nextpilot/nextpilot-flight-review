#pragma once

#include "IModule.h"
#include "ModuleInfo.h"
#include "Result.h"

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
class ModuleHost : public QObject
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

    // 执行意图：分派给声明了对应能力的模块。
    ng::core::Result<ng::core::IntentResult> execute(const ng::core::Intent &intent);

    bool isActive(const QString &moduleId) const;
    QStringList activeIds() const;
    QList<ModuleInfo> availableModules() const;

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
    QHash<QString, Node> m_nodes;
    QStringList m_order;
    QHash<QString, ng::core::IIntentHandler *> m_handlers; // op -> handler
    QString m_initializing; // 当前正在 initialize 的模块 id，用于 handler 归属
};

} // namespace ng::host
