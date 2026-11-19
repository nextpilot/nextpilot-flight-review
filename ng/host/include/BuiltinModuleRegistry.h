#pragma once

#include "ModuleInfo.h"

#include <QList>
#include <QString>
#include <functional>

namespace nextpilot::plotscope::host
{

class IModule;

using ModuleFactory = std::function<IModule *()>;

// 内置模块的**编译期自注册**。
//
// 为什么需要它：
//   需求是"增加一个 module 不修改其他代码"。若宿主靠 App 逐个
//   `registerModule(XxxModule().info(), factory)`，每加一个 module 都要改 App.cpp——
//   这正是需求要消灭的耦合。
//
//   改为：每个模块在自己的 .cpp 里用静态对象把自己登记进全局表，
//   宿主启动时遍历该表装载。于是"新增 module"= 新增一个目录 + 一行 CMake，
//   宿主、App、既有模块代码全都不动。
//
// 与工具箱（ExtensionRegistry）的关系：模块是"器官"，有生命周期、依赖拓扑、
// 可上 UI；工具箱是"业务能力"，无生命周期。二者机制不同，但都遵循
// "新增不修改既有代码"这条原则。
class BuiltinModuleRegistry
{
public:
    // 在模块 TU 的静态初始化期调用（见 NG_REGISTER_MODULE 宏）。
    // 返回 true 表示登记成功；重复 id 视为错误并忽略（保持先到者）。
    static bool registerModule(const ModuleInfo &info, ModuleFactory factory);

    // 全部已登记的模块（登记顺序）。
    static QList<QPair<ModuleInfo, ModuleFactory>> modules();

    // 清空（仅测试用：让同一进程内可重复装配）。
    static void reset();
};

// 注册宏：在模块 .cpp 文件末尾使用，把本模块登记进全局表。
//
// 用法：
//   NG_REGISTER_MODULE(DataModule)
//
// 其中 DataModule 需提供静态接口（无需实例化即可拿到元数据与工厂）：
//   static ModuleInfo moduleInfo();
//   static IModule *create();
#define NG_REGISTER_MODULE(Cls)                                                          \
    namespace {                                                                           \
    const bool g_ng_module_reg_ =                                                        \
        ::nextpilot::plotscope::host::BuiltinModuleRegistry::registerModule(                               \
            Cls::moduleInfo(),                                                            \
            []() -> ::nextpilot::plotscope::host::IModule * { return Cls::create(); });                     \
    }

} // namespace nextpilot::plotscope::host
