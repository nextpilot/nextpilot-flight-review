#pragma once

// A8b-M2 视图注册表：承担"视图注册"职责（开工令 52440794 §4）。
// UiSlotHub 降级为壳内部槽位表后，视图的声明（来自 contributes.views /
// viewsContainers）与内容提供者工厂统一归本表；壳只按容器 region + order 摆放，
// 不解释业务 id。
//
// 设计约束：
//   - registry 层不链 Qt Widgets：QWidget 只做前置声明，工厂签名照 PanelFactoryHub
//     （std::function<QWidget*(AppContext&, IIntentExecutor&, QWidget*)>），不 include 实现。
//   - 一个 view id 对应一个 provider 工厂（registerViewProvider，VSCode 对位
//     registerWebviewViewProvider/createTreeView）；重复注册先到者为准 + 诊断。
//   - 声明层（contributes）与运行层（provider 工厂）分离：先 mount 声明，再 register
//     工厂；unmount(owner) 时声明与工厂整批撤，无悬空。
//   - 全局 id 唯一：容器 id 与 view id 分表，跨表冲突由装配层诊断（这里不重复校验，
//     parser 已做 owner 内去重）。

#include "ContributionParser.h"
#include "IActionProvider.h"
#include "IEditorProvider.h"
#include "IDisposable.h"
#include "ResourceRef.h"

#include <QHash>
#include <QList>
#include <QString>

#include <functional>
#include <memory>

class QWidget;

namespace nextpilot::plotscope::context
{
class AppContext;
}
namespace nextpilot::plotscope::intent
{
class IIntentExecutor;
}

namespace nextpilot::plotscope::registry
{

class ViewRegistry
{
public:
    // 视图内容工厂：与 PanelFactoryHub::Factory 同签名，壳创建时注入上下文/执行器/父控件。
    using ViewFactory = std::function<QWidget *(nextpilot::plotscope::context::AppContext &,
                                                nextpilot::plotscope::intent::IIntentExecutor &,
                                                QWidget *)>;

    struct ProviderEntry
    {
        ViewFactory factory;
        QString owner; // 注册者，卸载时按它整批撤
        bool valid() const { return factory != nullptr; }
    };

    // registerViewProvider(viewId, factory) —— 一个 view 一个工厂，先到者为准。
    bool registerViewProvider(const QString &viewId, ViewFactory factory, const QString &owner);
    // 按 owner 整批撤工厂（provider 随 owner unmount 注销）。
    int unregisterOwner(const QString &owner);
    const ProviderEntry *provider(const QString &viewId) const
    {
        auto it = m_providers.constFind(viewId);
        return it == m_providers.constEnd() ? nullptr : &it.value();
    }
    QWidget *create(const QString &viewId, nextpilot::plotscope::context::AppContext &appCtx,
                    nextpilot::plotscope::intent::IIntentExecutor &executor, QWidget *parent) const;

    // 动作提供器（裁定 52441074 §2）：与 registerViewProvider 同层、同 owner 校验、
    // 同整批撤销。provider 是借用指针，生命周期归注册方；一个 slot 多 provider 时
    // 按注册顺序汇总（先到者条目在前），同 owner 重复注册该 slot 拒绝。
    struct ActionProviderEntry
    {
        IActionProvider *provider = nullptr; // 借用，不拥有
        QString owner;
        QString slotId;
    };
    bool registerActionProvider(const QString &owner, const QString &slotId, IActionProvider *provider);
    QList<ActionProviderEntry> actionProviders() const;
    QList<ActionProviderEntry> actionProvidersFor(const QString &slotId) const;

    // ---- 编辑器注册面（M3-a 第 2 笔，vscode.window.registerCustomEditorProvider 对位）----
    // owner 必须等于该 viewType 的声明者（contributes.customEditors），否则拒（防冒名）；
    // 未声明 viewType 一律拒（声明是运行层注册的前提）。
    // 返回可撤销句柄（dispose 即撤该条；owner 卸载时宿主整批撤）。
    std::unique_ptr<nextpilot::plotscope::registry::IDisposable>
    registerEditorProvider(const QString &viewType, nextpilot::plotscope::registry::IEditorProvider *provider,
                           const QString &owner);
    // 按 viewType 取 provider（未注册返回 nullptr；provider 是借用指针）。
    nextpilot::plotscope::registry::IEditorProvider *editorProvider(const QString &viewType) const;
    // 资源 → viewType 匹配：遍历声明，selector 的 mime 精确命中或 filenamePattern
    // 简单 glob（* / ? 通配，大小写不敏感）命中，按 priority 降序取第一个有运行层
    // provider 的 viewType；无命中返回空串。
    QString editorViewTypeFor(const nextpilot::plotscope::registry::ResourceRef &ref) const;

    // 声明层：把一份 contributes 的视图/容器声明登记进表（按 owner 分组）。
    void declare(const ContributesDoc &doc);
    // unmount(owner) 时撤该 owner 的全部声明 + provider + actionProvider。
    void undeclare(const QString &owner);

    QList<ContributedViewContainer> containersOf(const QString &owner) const;
    QList<ContributedView> viewsOf(const QString &owner) const;
    // 全部声明（跨 owner；IViewsService 读回用，裁定 52442006 Step 5）。
    QList<ContributedViewContainer> allContainers() const { return m_containers; }
    QList<ContributedView> allViews() const { return m_views; }
    QList<ContributedView> viewsIn(const QString &containerId) const;
    QStringList declaredOwners() const { return m_ownerOrder; }

    // 编辑器声明读回（M3-a；IEditorService 用）。
    QList<ContributedCustomEditor> allCustomEditors() const { return m_editors; }
    QList<ContributedCustomEditor> customEditorsOf(const QString &owner) const;
    // 该 viewType 的声明者 owner；未声明返回空串。
    QString editorOwner(const QString &viewType) const;

private:
    QHash<QString, ProviderEntry> m_providers; // viewId -> 工厂 + owner
    QList<ActionProviderEntry> m_actionProviders; // slotId -> 提供器（借用）+ owner
    QList<ContributedViewContainer> m_containers;
    QList<ContributedView> m_views;
    QList<QString> m_ownerOrder;
    QList<ContributedCustomEditor> m_editors;

    struct EditorProviderEntry
    {
        nextpilot::plotscope::registry::IEditorProvider *provider = nullptr; // 借用，不拥有
        QString owner;
        bool disposed = false;
    };
    QHash<QString, EditorProviderEntry> m_editorProviders; // viewType -> provider + owner
};

} // namespace nextpilot::plotscope::registry
