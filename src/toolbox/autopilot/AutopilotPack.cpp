#include "AutopilotPack.h"

#include "Contribution.h"
#include "UlogFormatParser.h"

using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;

namespace nextpilot::plotscope::ext
{

QString AutopilotPack::packId()
{
    return QStringLiteral("pack.autopilot");
}

std::unique_ptr<Pack> AutopilotPack::build()
{
    auto pack = std::make_unique<Pack>();
    pack->id = packId();
    pack->name = QStringLiteral("飞控工具箱");
    pack->domain = Domain::Autopilot;

    // ① 格式：ULog -> Dataset（复用流式解析器）
    pack->parsers.push_back(std::make_unique<UlogFormatParser>());

    // ② UI：往绘图页"曲线与轴"组填飞控专属入口（纯数据，headless 也能查询）。
    // 旧 menu.plot / dock.left / canvas.tool 挂载点已随死槽删除（D13）：
    // 调参面板与航迹工具暂无有效挂载点，不再作为占位动作贡献，待对应视图落地后回归。
    pack->uiActions.append({
        {QStringLiteral("ap.plot.attitude"), QStringLiteral("姿态曲线"),
         QStringLiteral("ribbon.group.plot.series"),
         QStringLiteral("plot.render"), QStringLiteral("{\"table\":\"vehicle_attitude\"}")}});

    // ③ 脚本：随包分发的飞控分析脚本（由脚本模块注入执行）
    pack->scripts.append({
        {QStringLiteral("lua"),
         QStringLiteral("ps.ap = { ctl = function() ps.log('autopilot control helpers') end }\n"),
         QStringList{QStringLiteral("ps.ap")},
         QStringLiteral("ap.script.helpers"), QStringLiteral("飞控脚本库")}});

    // ④ 数据：样例数据资产
    pack->dataAssets.append({
        {QStringLiteral("ap.sample.flight"), QStringLiteral("一次飞行样例"),
         QStringLiteral("data/autopilot/sample.ulg"), QStringLiteral("ulog"),
         QStringLiteral("含姿态与控制的短航段")}});

    // ⑤ 例子：可运行的示例工程
    pack->examples.append({
        {QStringLiteral("ap.ex.attitude"), QStringLiteral("姿态分析示例"),
         QStringLiteral("examples/autopilot/attitude.lua"), QStringLiteral("加载 ULog 并绘制姿态")}});

    // ⑥ 文档：领域知识
    pack->docs.append({
        {QStringLiteral("ap.doc.ulog"), QStringLiteral("ULog 协议说明"),
         QStringLiteral("docs/autopilot/ulog.md"), QStringLiteral("text/markdown")}});

    return pack;
}

} // namespace nextpilot::plotscope::ext

PS_REGISTER_PACK(::nextpilot::plotscope::ext::AutopilotPack)
