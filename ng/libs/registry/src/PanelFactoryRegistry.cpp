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

// 面板标题表：与工厂同批自注册的纯数据（停靠/中央页签的显示标题）。
// [静态纪律·合法形态] 进程一份、装满后只读。
registry::Registry<QString> &titles()
{
    return registry::StaticRegistry<QString>::table;
}
} // namespace

void PanelFactoryRegistry::registerPanel(const QString &type, Factory factory, const QString &title)
{
    if (type.isEmpty() || !factory)
        return;
    factories().insert(type, std::move(factory)); // 重复 id 由 Registry 保证「先到者为准」
    if (!title.isEmpty())
        titles().insert(type, title);
}

QString PanelFactoryRegistry::title(const QString &type)
{
    const QString *t = titles().find(type);
    return t ? *t : QString();
}

QWidget *PanelFactoryRegistry::create(const QString &type, nextpilot::plotscope::context::AppContext &appCtx,
                                       nextpilot::plotscope::intent::IIntentExecutor &executor, QWidget *parent)
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
