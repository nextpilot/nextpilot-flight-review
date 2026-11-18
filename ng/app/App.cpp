#include "App.h"

#include "DataModule.h"
#include "PlotModule.h"
#include "ScriptModule.h"
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
    // 器官（module）：有生命周期、可上 UI 的 app 功能。宿主按依赖拓扑装载。
    m_host.registerModule(DataModule().info(), [] { return static_cast<IModule *>(new DataModule()); });
    m_host.registerModule(PlotModule().info(), [] { return static_cast<IModule *>(new PlotModule()); });
    m_host.registerModule(ScriptModule().info(), [] { return static_cast<IModule *>(new ScriptModule()); });

    // 工具箱（extensions）：能力驱动，不参与器官生命周期。
    // 路径一——内置格式：集中自注册，App 完全不认识 CSV/ULog 这些具体类。
    ng::ext::registerBuiltinFormats(m_host.extensions());
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
