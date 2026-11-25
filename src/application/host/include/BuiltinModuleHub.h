#pragma once

#include "ModuleInfo.h"
#include "StaticAutoRegistry.h"

#include <QList>
#include <QString>
#include <functional>

namespace nextpilot::plotscope::host
{

class IWorkbenchContribution;

using ModuleFactory = std::function<IWorkbenchContribution *()>;

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
// 与工具箱（ToolboxHub）的关系：模块是"模块"，有生命周期、依赖拓扑、
// 可上 UI；工具箱是"业务能力"，无生命周期。二者机制不同，但都遵循
// "新增不修改既有代码"这条原则。
//
// 实现薄壳：存储/查重/先到者为准全部委托 StaticHub<ModuleEntry>（机制唯一，
// 惰性 Meyers 表免疫跨 TU 初始化顺序），本壳只保留 modules() 的类型投影。
// M2.8 R4：树上的 BuiltinModuleNode 节点宿主经 modules() 枚举（diag.registries）。

// 一条模块登记：元数据 + 工厂。
struct ModuleEntry
{
    ModuleInfo info;
    ModuleFactory factory;

    bool valid() const { return !info.id.isEmpty() && factory != nullptr; }
};

class BuiltinModuleHub : public nextpilot::plotscope::registry::StaticHub<ModuleEntry>
{
public:
    // 在模块 TU 的静态初始化期调用（见 PS_REGISTER_MODULE 宏）。
    // 返回 true 表示登记成功；重复 id 视为错误并忽略（保持先到者）。
    static bool registerModule(const ModuleInfo &info, ModuleFactory factory)
    {
        return insert(info.id, ModuleEntry{info, std::move(factory)});
    }

    // 全部已登记的模块（登记顺序，登记顺序即装载顺序）。
    static QList<QPair<ModuleInfo, ModuleFactory>> modules()
    {
        QList<QPair<ModuleInfo, ModuleFactory>> out;
        for (const ModuleEntry &e : values())
            out.append({e.info, e.factory});
        return out;
    }

    // 清空（仅测试用：让同一进程内可重复装配）——继承自 StaticHub::reset()。

private:
    static QList<ModuleEntry> values()
    {
        QList<ModuleEntry> out;
        for (const QString &id : keys())
        {
            const ModuleEntry *e = find(id);
            if (e)
                out.append(*e);
        }
        return out;
    }
};

// 注册宏：在模块 .cpp 文件末尾使用，把本模块登记进全局表。
//
// 用法：
//   PS_REGISTER_MODULE(DataModule)
//
// 其中 DataModule 需提供静态接口（无需实例化即可拿到元数据与工厂）：
//   static ModuleInfo moduleInfo();
//   static IWorkbenchContribution *create();
#define PS_REGISTER_MODULE(Cls)                                                                                        \
    namespace {                                                                                                        \
    const bool g_ps_module_reg_ = ::nextpilot::plotscope::host::BuiltinModuleHub::registerModule(                      \
        Cls::moduleInfo(), []() -> ::nextpilot::plotscope::host::IWorkbenchContribution * { return Cls::create(); });  \
    }

} // namespace nextpilot::plotscope::host
