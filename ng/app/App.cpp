#include "App.h"

#include "BuiltinModuleRegistry.h"
#include "Toolbox.h"
#include "PluginScanner.h"
#include "RecordingBackend.h"
#include "RecordingFormat.h"

#include <QCoreApplication>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;
using namespace nextpilot::plotscope::context;
using namespace nextpilot::plotscope::rec;
using namespace nextpilot::plotscope::host;

namespace nextpilot::plotscope::app
{

App::App(const QString &settingsPath, QObject *parent)
    : QObject(parent), m_ctx(settingsPath, this), m_host(m_ctx, this)
{
    // 器官（module）：app 的组成部分，按依赖拓扑装载。
    // 模块在各自 TU 里静态自注册（NG_REGISTER_MODULE），这里只把已登记的
    // 全部装进宿主——新增一个 module 不用改本文件。
    const auto &builtin = BuiltinModuleRegistry::modules();
    for (const auto &entry : builtin)
        m_host.registerModule(entry.first, entry.second);

    // 工具箱（extensions）：业务能力包，不参与器官生命周期。
    // 路径一——内置知识包：各自静态自注册，装载时统一收进工具箱。
    nextpilot::plotscope::ext::registerBuiltinPacks(m_ctx.toolbox());
    // 路径二——外部插件：扫描目录、校验后注册进同一个工具箱（本轮仅预留入口）。
    // 目录可后续由配置/命令行注入；默认检查可执行文件旁的 plugins/ 目录。
    nextpilot::plotscope::ext::scanExternalPlugins(m_ctx.toolbox(),
                                 QCoreApplication::applicationDirPath() + QStringLiteral("/plugins"));

    // 录制：格式 × 后端两个独立注册表。
    // 格式决定"字节长什么样"（raw/mcap），后端决定"字节送到哪"（文件/UDP）。
    // 正交组合使"同时录多种格式、多种后端"成为自然结果。
    // 二者都是 app 的机制选项，不含领域语义——不走知识包贡献体系。
    nextpilot::plotscope::rec::RecordingFormatRegistry::installBuiltinFormats();
    nextpilot::plotscope::rec::RecordingBackendRegistry::installBuiltinBackends();
}

QHash<QString, QString> App::startup(bool headless, const QStringList &disabled)
{
    // 录制器挂到宿主：脚本执行时门面会把业务意图录进来。
    m_host.setRecorder(&m_recorder);
    // headless 时 ui 为空 -> 没有任何模块创建 QWidget。
    return m_host.activate(disabled, nullptr);
}

} // namespace nextpilot::plotscope::app
