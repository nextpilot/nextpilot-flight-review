#include "App.h"

#include "BuiltinModuleRegistry.h"
#include "FormatToolbox.h"
#include "PluginScanner.h"

#include <QCoreApplication>

using namespace ng::core;
using namespace ng::host;

namespace ng::app
{

App::App(QObject *parent)
    : QObject(parent), m_repo(this), m_host(m_repo, this)
{
    // 器官（module）：app 的组成部分，按依赖拓扑装载。
    // 模块在各自 TU 里静态自注册（NG_REGISTER_MODULE），这里只把已登记的
    // 全部装进宿主——新增一个 module 不用改本文件。
    const auto &builtin = BuiltinModuleRegistry::modules();
    for (const auto &entry : builtin)
        m_host.registerModule(entry.first, entry.second);

    // 工具箱（extensions）：业务能力包，不参与器官生命周期。
    // 路径一——内置知识包：各自静态自注册，装载时统一收进工具箱。
    ng::ext::registerBuiltinPacks(m_host.extensions());
    // 路径二——外部插件：扫描目录、校验后注册进同一个工具箱（本轮仅预留入口）。
    // 目录可后续由配置/命令行注入；默认检查可执行文件旁的 plugins/ 目录。
    ng::ext::scanExternalPlugins(m_host.extensions(),
                                 QCoreApplication::applicationDirPath() + QStringLiteral("/plugins"));
}

QHash<QString, QString> App::startup(bool headless, const QStringList &disabled)
{
    // 录制器挂到宿主：脚本执行时门面会把业务意图录进来。
    m_host.setRecorder(&m_recorder);
    // headless 时 ui 为空 -> 没有任何模块创建 QWidget。
    return m_host.activate(disabled, nullptr);
}

} // namespace ng::app
