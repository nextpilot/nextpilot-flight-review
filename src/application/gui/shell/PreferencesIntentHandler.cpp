#include "PreferencesIntentHandler.h"

#include "AppContext.h"
#include "PreferencesDialog.h"
#include "RuntimeHost.h"

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::context;
using namespace nextpilot::plotscope::host;

namespace nextpilot::plotscope::gui
{

PreferencesIntentHandler::PreferencesIntentHandler(AppContext &appCtx, RuntimeHost &host,
                                                  QObject *parent)
    : QObject(parent), m_appCtx(appCtx), m_host(host)
{
}

bool PreferencesIntentHandler::canHandle(const Intent &intent) const
{
    return intent.op == QStringLiteral("preferences.open");
}

Result<IntentResult>
PreferencesIntentHandler::execute(const Intent &intent)
{
    // section 定位首屏分组（数据源面板的「数据设置…」传 "data"）；
    // 空 = 默认「外观」页。
    show(intent.params.value(QStringLiteral("section")).toString());
    IntentResult r;
    r.payload = true;
    return r;
}

void PreferencesIntentHandler::show(const QString &section)
{
    // 模态：与「文件→设置」在其它应用里的习惯一致（打开即独占，关掉才回来）。
    // 对话框自己负责读写与落盘（settings.set + settings.flush），这里不做业务。
    PreferencesDialog dlg(m_appCtx, m_host, section, nullptr);
    dlg.exec();
}

} // namespace nextpilot::plotscope::gui
