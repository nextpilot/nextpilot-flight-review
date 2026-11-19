#include "PanelFactoryRegistry.h"

#include "AppContext.h"
#include "IIntentExecutor.h"

// B3：泛型注册表核 + 自注册助手。本文件退化为薄壳——
// 容器行为（增删查遍历）委托给 registry::Registry，自身只保留类型定义与对外 API。
#include "Registry.h"
#include "StaticAutoRegistry.h"

namespace nextpilot::plotscope::core
{

namespace
{
// 静态共享表：C++17 inline 变量（铁律允许的「静态对象」例外），启动期构造。
// 对外 registerPanel/create/types 与 NG_REGISTER_PANEL 宏完全不变，调用方零改动。
registry::Registry<PanelFactoryRegistry::Factory> &factories()
{
    return registry::StaticRegistry<PanelFactoryRegistry::Factory>::table;
}
} // namespace

void PanelFactoryRegistry::registerPanel(const QString &type, Factory factory)
{
    if (type.isEmpty() || !factory)
        return;
    factories().insert(type, std::move(factory)); // 重复 id 由 Registry 保证「先到者为准」
}

QWidget *PanelFactoryRegistry::create(const QString &type, nextpilot::plotscope::core::AppContext &appCtx,
                                       nextpilot::plotscope::core::IIntentExecutor &executor, QWidget *parent)
{
    auto *factory = factories().find(type);
    if (!factory)
        return nullptr;
    return (*factory)(appCtx, executor, parent);
}

QStringList PanelFactoryRegistry::types()
{
    QStringList out;
    for (const auto &key : factories().keys())
        out.append(key);
    out.sort();
    return out;
}

} // namespace nextpilot::plotscope::core
