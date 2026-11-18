#include "PanelFactoryRegistry.h"

#include "AppContext.h"
#include "IIntentExecutor.h"

#include <QWidget>

namespace ng::gui
{

namespace
{
QHash<QString, PanelFactoryRegistry::Factory> &factories()
{
    static QHash<QString, PanelFactoryRegistry::Factory> map;
    return map;
}
} // namespace

void PanelFactoryRegistry::registerPanel(const QString &type, Factory factory)
{
    if (type.isEmpty() || !factory)
        return;
    if (factories().contains(type))
        return; // 先到者为准
    factories().insert(type, std::move(factory));
}

QWidget *PanelFactoryRegistry::create(const QString &type, ng::core::AppContext &appCtx,
                                       ng::core::IIntentExecutor &executor, QWidget *parent)
{
    auto it = factories().constFind(type);
    if (it == factories().constEnd())
        return nullptr;
    return it.value()(appCtx, executor, parent);
}

QStringList PanelFactoryRegistry::types()
{
    QStringList out;
    for (auto it = factories().constBegin(); it != factories().constEnd(); ++it)
        out.append(it.key());
    out.sort();
    return out;
}

} // namespace ng::gui
