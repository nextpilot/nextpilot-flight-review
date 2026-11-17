#pragma once

#include "DataRepository.h"
#include "IIntentExecutor.h"
#include "Intent.h"
#include "JobRecorder.h"
#include "ModuleInfo.h"

#include <QObject>

class QWidget;

namespace ng::host
{

class ModuleHost;

// UI 上下文：headless 时宿主不构造它，模块 onAttach 就不会被调用。
// 它只提供"放置"能力，不接管 UI 对象的所有权——所有权始终在模块。
class UiContext
{
public:
    virtual ~UiContext() = default;
    virtual void placePanel(QWidget *widget, const QString &area) = 0;
};

// 模块拿到的运行时上下文：只有能力，没有全局单例、没有 MainWindow*。
class ModuleContext
{
public:
    ModuleContext(ng::core::DataRepository &repo, ModuleHost &host)
        : m_repo(repo), m_host(host) {}

    ng::core::DataRepository &data() { return m_repo; }
    ModuleHost &host() { return m_host; }

    // 脚本/自动化能力：拿到意图执行器与可选录制器。
    // 返回的是宿主实现的执行器抽象，模块只依赖接口不依赖宿主内部。
    ng::core::IIntentExecutor &executor();
    ng::core::JobRecorder *recorder();

    // 模块注册自己能处理的意图 op。
    void registerIntentHandler(ng::core::IIntentHandler *handler);

private:
    ng::core::DataRepository &m_repo;
    ModuleHost &m_host;
};

// 统一模块接口。器官与知识包都实现它，宿主一视同仁地装载。
class IModule : public QObject
{
    Q_OBJECT
public:
    explicit IModule(QObject *parent = nullptr) : QObject(parent) {}
    ~IModule() override = default;

    virtual ng::host::ModuleInfo info() const = 0;

    // 阶段一：声明能力、订阅事件、注册意图处理器。不碰 UI。
    virtual void initialize(ModuleContext &ctx) = 0;

    // 阶段二：创建 UI 并 parent 给自己，然后请求宿主放置。
    // headless 时宿主不调用；默认空实现。
    virtual void onAttach(UiContext &) {}

    // 阶段三：可选，模块被卸载前的清理。QObject 析构负责资源回收。
    virtual void onDetach() {}
};

} // namespace ng::host
