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

    // 领域面板**描述**（纯数据，headless 可见）：
    // 汽车包提供 CAN 报文面板——它的形态由 DBC 定义的信号决定，
    // 属于领域知识，通用视图层（gui/views/）不该认识它。
    // A2c：实现归位 views 声明 + provider（AutomotiveViewFactories，owner
    // "automotive"，view id automotive.canBrowser）——壳经 IViewsService 消费，
    // 面板描述不再带落区枚举（落区由 viewsContainers[].area 决定，模块不指定 Region）。
    pack->panels.append({QStringLiteral("automotive.canBrowser"), QStringLiteral("CAN 报文")});

    pack->docs.append({
        {QStringLiteral("car.doc.formats"), QStringLiteral("汽车总线格式（待接入）"),
         QStringLiteral("docs/automotive/formats.md"), QStringLiteral("text/markdown")}});

    return pack;
}

} // namespace nextpilot::plotscope::ext

PS_REGISTER_PACK(::nextpilot::plotscope::ext::AutomotivePack)
