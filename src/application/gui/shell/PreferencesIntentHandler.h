#pragma once

#include "Intent.h"

#include <QObject>

namespace nextpilot::plotscope::host
{
class RuntimeHost;
}

namespace nextpilot::plotscope::gui
{

// `preferences.open` —— 打开设置：普通编辑器页签，无模态对话框。
// 本处理器只做意图转派——最终执行体是 workbench.action.openSettings
//（IEditorService::openEditor(workbench.editor.settings2, "settings:" 资源)）。
// 设置 UI 归工作台内置 Settings editor；壳与模块都不提供界面、不自己持久化。
class PreferencesIntentHandler final : public QObject, public intent::IIntentHandler
{
    Q_OBJECT
public:
    explicit PreferencesIntentHandler(host::RuntimeHost &host, QObject *parent = nullptr);

    bool canHandle(const intent::Intent &intent) const override;
    nextpilot::plotscope::foundation::Result<intent::IntentResult>
    execute(const intent::Intent &intent) override;

private:
    host::RuntimeHost &m_host;
};

} // namespace nextpilot::plotscope::gui
