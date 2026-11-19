#include "AutopilotPack.h"

#include "BuiltinContributions.h"
#include "BuiltinPackRegistry.h"
#include "UlogFormatParser.h"

using namespace nextpilot::plotscope::core;

namespace nextpilot::plotscope::ext
{

QString AutopilotPack::packId()
{
    return QStringLiteral("pack.autopilot");
}

std::unique_ptr<IToolPack> AutopilotPack::build()
{
    auto pack = std::make_unique<ToolPack>(packId(), QStringLiteral("飞控工具箱"), Domain::Autopilot);

    // ① 格式：ULog -> Dataset（复用流式解析器）
    pack->add<UlogFormatParser>();

    // ② UI：往 app 骨架提供的插槽里填飞控专属入口（纯数据，headless 也能查询）
    pack->add<GenericUiContribution>(
        QStringLiteral("ap.ui.contributions"), QStringLiteral("飞控界面入口"), Domain::Autopilot,
        QList<UiAction>{
            {QStringLiteral("ap.plot.attitude"), QStringLiteral("姿态曲线"), QStringLiteral("menu.plot"),
             QStringLiteral("plot.render"), QStringLiteral("{\"table\":\"vehicle_attitude\"}")},
            {QStringLiteral("ap.dock.tuning"), QStringLiteral("调参面板"), QStringLiteral("dock.left"),
             QString(), QString()},
            {QStringLiteral("ap.canvas.trajectory"), QStringLiteral("航迹工具"), QStringLiteral("canvas.tool"),
             QString(), QString()}});

    // ③ 脚本：随包分发的飞控分析脚本（由脚本器官注入执行）
    pack->add<GenericScriptContribution>(
        QStringLiteral("ap.script.helpers"), QStringLiteral("飞控脚本库"), Domain::Autopilot,
        QStringLiteral("lua"),
        QStringLiteral("ps.ap = { ctl = function() ps.log('autopilot control helpers') end }\n"),
        QStringList{QStringLiteral("ps.ap")});

    // ④ 数据：样例数据资产
    pack->add<GenericDataContribution>(
        QStringLiteral("ap.data.samples"), QStringLiteral("飞控样例数据"), Domain::Autopilot,
        QList<DataAsset>{
            {QStringLiteral("ap.sample.flight"), QStringLiteral("一次飞行样例"),
             QStringLiteral("data/autopilot/sample.ulg"), QStringLiteral("ulog"),
             QStringLiteral("含姿态与控制的短航段")}});

    // ⑤ 例子：可运行的示例工程
    pack->add<GenericExampleContribution>(
        QStringLiteral("ap.examples"), QStringLiteral("飞控示例"), Domain::Autopilot,
        QList<ExampleEntry>{
            {QStringLiteral("ap.ex.attitude"), QStringLiteral("姿态分析示例"),
             QStringLiteral("examples/autopilot/attitude.lua"), QStringLiteral("加载 ULog 并绘制姿态")}});

    // ⑥ 文档：领域知识
    pack->add<GenericDocContribution>(
        QStringLiteral("ap.docs"), QStringLiteral("飞控知识文档"), Domain::Autopilot,
        QList<DocEntry>{
            {QStringLiteral("ap.doc.ulog"), QStringLiteral("ULog 协议说明"),
             QStringLiteral("docs/autopilot/ulog.md"), QStringLiteral("text/markdown")}});

    return pack;
}

QString AutopilotPack::id() const { return m_pack.id(); }
QString AutopilotPack::displayName() const { return m_pack.displayName(); }
Domain AutopilotPack::domain() const { return m_pack.domain(); }
const std::vector<std::unique_ptr<IToolContribution>> &AutopilotPack::contributions() const
{
    return m_pack.contributions();
}
std::vector<std::unique_ptr<IToolContribution>> &AutopilotPack::contributions()
{
    return m_pack.contributions();
}

} // namespace nextpilot::plotscope::ext

NG_REGISTER_PACK(::nextpilot::plotscope::ext::AutopilotPack)
