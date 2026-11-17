#include "App.h"

#include "DataModule.h"
#include "PlotModule.h"
#include "CsvExtension.h"

using namespace ng::core;
using namespace ng::host;

namespace ng::app
{

App::App(QObject *parent)
    : QObject(parent), m_repo(this), m_host(m_repo, this)
{
    // 编译期注册：器官与知识包走同一个入口，宿主一视同仁。
    m_host.registerModule(DataModule().info(), [] { return static_cast<IModule *>(new DataModule()); });
    m_host.registerModule(PlotModule().info(), [] { return static_cast<IModule *>(new PlotModule()); });
    m_host.registerModule(CsvExtension().info(), [] { return static_cast<IModule *>(new CsvExtension()); });
}

QHash<QString, QString> App::startup(bool headless, const QStringList &disabled)
{
    // headless 时 ui 为空 -> 没有任何模块创建 QWidget。
    return m_host.activate(disabled, nullptr);
}

} // namespace ng::app
