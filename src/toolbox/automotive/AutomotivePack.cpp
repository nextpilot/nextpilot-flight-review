#include "AutomotivePack.h"

#include "Contribution.h"

using namespace nextpilot::plotscope::registry;

namespace nextpilot::plotscope::ext
{

QString AutomotivePack::packId()
{
    return QStringLiteral("pack.automotive");
}

std::unique_ptr<Pack> AutomotivePack::build()
{
    // 预留领域：先以 UI + 文档占位，格式驱动后续按 ULog 公式平移时落入本包。
    auto pack = std::make_unique<Pack>();
    pack->id = packId();
    pack->name = QStringLiteral("汽车工具箱");
    pack->domain = Domain::Automotive;

    // A2d 第②步：旧包面板声明面（Pack.panels）退场——CAN 报文面板只剩
    // contributes.views 单一声明通道（AutomotiveViewFactories 装配，owner
    // "automotive"，view id automotive.canBrowser；落区由 viewsContainers[].area
    // 决定，模块不指定 Region）。
    pack->docs.append({{QStringLiteral("car.doc.formats"), QStringLiteral("汽车总线格式（待接入）"),
                        QStringLiteral("docs/automotive/formats.md"), QStringLiteral("text/markdown")}});

    return pack;
}

} // namespace nextpilot::plotscope::ext

PS_REGISTER_PACK(::nextpilot::plotscope::ext::AutomotivePack)
