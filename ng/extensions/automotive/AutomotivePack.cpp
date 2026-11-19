#include "AutomotivePack.h"

#include "BuiltinContributions.h"
#include "BuiltinPackRegistry.h"

using namespace nextpilot::plotscope::core;

namespace nextpilot::plotscope::ext
{

QString AutomotivePack::packId()
{
    return QStringLiteral("pack.automotive");
}

std::unique_ptr<IToolPack> AutomotivePack::build()
{
    // 预留领域：先以 UI + 文档占位，格式驱动后续按 ULog 公式平移时落入本包。
    auto pack = std::make_unique<ToolPack>(packId(), QStringLiteral("汽车工具箱"), Domain::Automotive);
    pack->add<GenericUiContribution>(
        QStringLiteral("car.ui.contributions"), QStringLiteral("汽车界面入口"), Domain::Automotive,
        QList<UiAction>{
            {QStringLiteral("car.dock.can"), QStringLiteral("CAN 报文面板"), QStringLiteral("dock.right"),
             QString(), QString()}});
    // 领域面板**描述**（纯数据，headless 可见）：
    // 汽车包提供 CAN 报文面板——它的形态由 DBC 定义的信号决定，
    // 属于领域知识，通用视图层（gui/views/）不该认识它。
    // 实现见 ng/extensions/automotive/gui/CanBrowserPanel.cpp（GUI 侧）。
    pack->add<GenericPanelContribution>(
        QStringLiteral("car.panels"), QStringLiteral("汽车领域面板"), Domain::Automotive,
        QList<PanelSpec>{
            {QStringLiteral("car.can-browser"), QStringLiteral("CAN 报文"), QStringLiteral("dock.right")}});

    pack->add<GenericDocContribution>(
        QStringLiteral("car.docs"), QStringLiteral("汽车知识文档"), Domain::Automotive,
        QList<DocEntry>{
            {QStringLiteral("car.doc.formats"), QStringLiteral("汽车总线格式（待接入）"),
             QStringLiteral("docs/automotive/formats.md"), QStringLiteral("text/markdown")}});
    return pack;
}

QString AutomotivePack::id() const { return m_pack.id(); }
QString AutomotivePack::displayName() const { return m_pack.displayName(); }
Domain AutomotivePack::domain() const { return m_pack.domain(); }
const std::vector<std::unique_ptr<IToolContribution>> &AutomotivePack::contributions() const
{
    return m_pack.contributions();
}
std::vector<std::unique_ptr<IToolContribution>> &AutomotivePack::contributions()
{
    return m_pack.contributions();
}

} // namespace nextpilot::plotscope::ext

NG_REGISTER_PACK(::nextpilot::plotscope::ext::AutomotivePack)
