#pragma once

// A8b-M3-a 第 2 笔：编辑器声明/注册的读回与打开经本服务——壳不直接持有 ViewRegistry，
// 不 include registry:: 类型。实现放 assembly（EditorServiceAdapter，内部转 ViewRegistry）。
// VSCode 对位 `vscode.window.registerCustomEditorProvider` 的消费面 + customEditors 查询。

#include "ContributionParser.h" // ContributedCustomEditor（纯数据）
#include "IEditorProvider.h"
#include "ResourceRef.h"

#include <QList>
#include <QString>

class QWidget;

namespace nextpilot::plotscope::context
{
class AppContext;
}
namespace nextpilot::plotscope::intent
{
class IIntentExecutor;
}

namespace nextpilot::plotscope::workbench
{

class IEditorService
{
public:
    virtual ~IEditorService() = default;

    // 当前声明的全部自定义编辑器（跨 owner，按声明序；IEditorService 读回用）。
    virtual QList<nextpilot::plotscope::registry::ContributedCustomEditor> customEditors() const = 0;

    // 资源 → viewType 匹配（selector 的 mime / filenamePattern，priority 降序；
    // 须有运行层 provider 才算命中）。未命中返回空串。
    virtual QString editorViewTypeFor(const registry::ResourceRef &ref) const = 0;

    // 按 viewType 取 provider（未注册返回 nullptr；借用指针）。
    virtual nextpilot::plotscope::registry::IEditorProvider *providerFor(const QString &viewType) const = 0;

    // 打开编辑器内容（createEditor 转发；viewType 未注册或 provider 拒开返回 nullptr）。
    virtual QWidget *openEditor(const QString &viewType, const registry::ResourceRef &ref,
                                nextpilot::plotscope::context::AppContext &appCtx,
                                nextpilot::plotscope::intent::IIntentExecutor &executor,
                                QWidget *parent) = 0;
};

// 纯接口（无 QObject 基类）：registerSingleton 存接口地址，getService static_cast 恒安全。
} // namespace nextpilot::plotscope::workbench
