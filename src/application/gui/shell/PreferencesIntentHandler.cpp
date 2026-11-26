#include "PreferencesIntentHandler.h"

#include "RuntimeHost.h"

using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::host;

namespace nextpilot::plotscope::gui
{

PreferencesIntentHandler::PreferencesIntentHandler(RuntimeHost &host, QObject *parent) : QObject(parent), m_host(host)
{
}

bool PreferencesIntentHandler::canHandle(const Intent &intent) const
{
    return intent.op == QStringLiteral("preferences.open");
}

foundation::Result<IntentResult> PreferencesIntentHandler::execute(const Intent &intent)
{
    // 转派到工作台内置命令——最终执行体唯一（WorkbenchCommandHandler →
    // IEditorService::openEditor(workbench.editor.settings2, settings: 资源)）。
    // 本处理器不另造对话框、不重复实现打开逻辑，也不让壳碰编辑器服务。
    Intent open;
    open.op = QStringLiteral("workbench.action.openSettings");
    open.params = intent.params;
    return m_host.execute(open);
}

} // namespace nextpilot::plotscope::gui
