#include "RoboticsPack.h"

#include "BuiltinContributions.h"
#include "BuiltinPackRegistry.h"

using namespace nextpilot::plotscope::core;

namespace nextpilot::plotscope::ext
{

QString RoboticsPack::packId()
{
    return QStringLiteral("pack.robotics");
}

std::unique_ptr<IToolPack> RoboticsPack::build()
{
    // 预留领域：机器人消息中间件（MCAP / ROSbag2）格式后续平移时落入本包。
    auto pack = std::make_unique<ToolPack>(packId(), QStringLiteral("机器人工具箱"), Domain::Robotics);
    pack->add<GenericUiContribution>(
        QStringLiteral("rob.ui.contributions"), QStringLiteral("机器人界面入口"), Domain::Robotics,
        QList<UiAction>{
            {QStringLiteral("rob.plot.tf"), QStringLiteral("TF 树可视化"), QStringLiteral("menu.plot"),
             QString(), QString()}});
    pack->add<GenericDocContribution>(
        QStringLiteral("rob.docs"), QStringLiteral("机器人知识文档"), Domain::Robotics,
        QList<DocEntry>{
            {QStringLiteral("rob.doc.mcap"), QStringLiteral("MCAP / ROSbag2（待接入）"),
             QStringLiteral("docs/robotics/mcap.md"), QStringLiteral("text/markdown")}});
    return pack;
}

QString RoboticsPack::id() const { return m_pack.id(); }
QString RoboticsPack::displayName() const { return m_pack.displayName(); }
Domain RoboticsPack::domain() const { return m_pack.domain(); }
const std::vector<std::unique_ptr<IToolContribution>> &RoboticsPack::contributions() const
{
    return m_pack.contributions();
}
std::vector<std::unique_ptr<IToolContribution>> &RoboticsPack::contributions()
{
    return m_pack.contributions();
}

} // namespace nextpilot::plotscope::ext

NG_REGISTER_PACK(::nextpilot::plotscope::ext::RoboticsPack)
