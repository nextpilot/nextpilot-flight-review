#include "PanelFactoryRegistry.h"

#include "IIntentExecutor.h"

// B3：容器换成泛型 Registry，对外 API 与 NG_REGISTER_PANEL 宏不变。
#include "Registry.h"
#include "StaticAutoRegistry.h"

namespace nextpilot::plotscope::registry
{

namespace
{
// 面板工厂表：启动期自注册，运行期只读。
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

} // namespace nextpilot::plotscope::registry
