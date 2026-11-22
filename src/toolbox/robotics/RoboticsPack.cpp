#include "RoboticsPack.h"

#include "Contribution.h"

using namespace nextpilot::plotscope::registry;

namespace nextpilot::plotscope::ext
{

QString RoboticsPack::packId()
{
    return QStringLiteral("pack.robotics");
}

std::unique_ptr<Pack> RoboticsPack::build()
{
    // 预留领域：机器人消息中间件（MCAP / ROSbag2）格式后续平移时落入本包。
    auto pack = std::make_unique<Pack>();
    pack->id = packId();
    pack->name = QStringLiteral("机器人工具箱");
    pack->domain = Domain::Robotics;

    pack->uiActions.append({
        {QStringLiteral("rob.plot.tf"), QStringLiteral("TF 树可视化"), QStringLiteral("menu.plot"),
         QString(), QString()}});

    pack->docs.append({
        {QStringLiteral("rob.doc.mcap"), QStringLiteral("MCAP / ROSbag2（待接入）"),
         QStringLiteral("docs/robotics/mcap.md"), QStringLiteral("text/markdown")}});

    return pack;
}

} // namespace nextpilot::plotscope::ext

PS_REGISTER_PACK(::nextpilot::plotscope::ext::RoboticsPack)
