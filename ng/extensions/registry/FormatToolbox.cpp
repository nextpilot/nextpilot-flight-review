#include "FormatToolbox.h"
#include "BuiltinContributions.h"

#include "CsvFormatDriver.h"
#include "UlogFormatDriver.h"

#include <memory>

using namespace ng::core;

namespace ng::ext
{

// 组装一个领域的知识包：格式 + UI + 脚本 + 数据 + 例子 + 文档，
// 按需放入对应贡献。每个包是装载/卸载的原子单位。
static std::unique_ptr<ToolPack> buildCommonPack()
{
    auto pack = std::make_unique<ToolPack>(QStringLiteral("pack.common"),
                                           QStringLiteral("通用工具箱"), FormatDomain::General);
    pack->add<CsvFormatDriver>(); // 格式：文件 -> Dataset
    pack->add<GenericDocContribution>(
        QStringLiteral("common.doc.formats"), QStringLiteral("通用格式说明"), FormatDomain::General,
        QList<DocEntry>{
            {QStringLiteral("common.doc.csv"), QStringLiteral("CSV 载入指南"),
             QStringLiteral("docs/common/csv.md"), QStringLiteral("text/markdown")}});
    return pack;
}

static std::unique_ptr<ToolPack> buildAutopilotPack()
{
    auto pack = std::make_unique<ToolPack>(QStringLiteral("pack.autopilot"),
                                           QStringLiteral("飞控工具箱"), FormatDomain::Autopilot);

    // ① 格式：ULog -> Dataset（复用流式解析器）
    pack->add<UlogFormatDriver>();

    // ② UI：往器官提供的插槽里填飞控专属入口（纯数据，headless 也能查询）
    pack->add<GenericUiContribution>(
        QStringLiteral("ap.ui.contributions"), QStringLiteral("飞控界面入口"), FormatDomain::Autopilot,
        QList<UiAction>{
            {QStringLiteral("ap.plot.attitude"), QStringLiteral("姿态曲线"), QStringLiteral("menu.plot"),
             QStringLiteral("plot.render"), QStringLiteral("{\"table\":\"vehicle_attitude\"}")},
            {QStringLiteral("ap.dock.tuning"), QStringLiteral("调参面板"), QStringLiteral("dock.left"),
             QString(), QString()},
            {QStringLiteral("ap.canvas.trajectory"), QStringLiteral("航迹工具"), QStringLiteral("canvas.tool"),
             QString(), QString()}});

    // ③ 脚本：随包分发的飞控分析脚本（由脚本器官注入执行）
    pack->add<GenericScriptContribution>(
        QStringLiteral("ap.script.helpers"), QStringLiteral("飞控脚本库"), FormatDomain::Autopilot,
        QStringLiteral("lua"),
        QStringLiteral("ps.ap = { ctl = function() ps.log('autopilot control helpers') end }\n"),
        QStringList{QStringLiteral("ps.ap")});

    // ④ 数据：样例数据资产
    pack->add<GenericDataContribution>(
        QStringLiteral("ap.data.samples"), QStringLiteral("飞控样例数据"), FormatDomain::Autopilot,
        QList<DataAsset>{
            {QStringLiteral("ap.sample.flight"), QStringLiteral("一次飞行样例"),
             QStringLiteral("data/autopilot/sample.ulg"), QStringLiteral("ulog"),
             QStringLiteral("含姿态与控制的短航段")}});

    // ⑤ 例子：可运行的示例工程
    pack->add<GenericExampleContribution>(
        QStringLiteral("ap.examples"), QStringLiteral("飞控示例"), FormatDomain::Autopilot,
        QList<ExampleEntry>{
            {QStringLiteral("ap.ex.attitude"), QStringLiteral("姿态分析示例"),
             QStringLiteral("examples/autopilot/attitude.lua"), QStringLiteral("加载 ULog 并绘制姿态")}});

    // ⑥ 文档：领域知识
    pack->add<GenericDocContribution>(
        QStringLiteral("ap.docs"), QStringLiteral("飞控知识文档"), FormatDomain::Autopilot,
        QList<DocEntry>{
            {QStringLiteral("ap.doc.ulog"), QStringLiteral("ULog 协议说明"),
             QStringLiteral("docs/autopilot/ulog.md"), QStringLiteral("text/markdown")}});

    return pack;
}

static std::unique_ptr<ToolPack> buildAutomotivePack()
{
    // 预留领域：先以 UI + 文档占位，格式驱动后续按 ULog 公式平移时落入本包。
    auto pack = std::make_unique<ToolPack>(QStringLiteral("pack.automotive"),
                                           QStringLiteral("汽车工具箱"), FormatDomain::Automotive);
    pack->add<GenericUiContribution>(
        QStringLiteral("car.ui.contributions"), QStringLiteral("汽车界面入口"), FormatDomain::Automotive,
        QList<UiAction>{
            {QStringLiteral("car.dock.can"), QStringLiteral("CAN 报文面板"), QStringLiteral("dock.right"),
             QString(), QString()}});
    pack->add<GenericDocContribution>(
        QStringLiteral("car.docs"), QStringLiteral("汽车知识文档"), FormatDomain::Automotive,
        QList<DocEntry>{
            {QStringLiteral("car.doc.formats"), QStringLiteral("汽车总线格式（待接入）"),
             QStringLiteral("docs/automotive/formats.md"), QStringLiteral("text/markdown")}});
    return pack;
}

static std::unique_ptr<ToolPack> buildRoboticsPack()
{
    auto pack = std::make_unique<ToolPack>(QStringLiteral("pack.robotics"),
                                           QStringLiteral("机器人工具箱"), FormatDomain::Robotics);
    pack->add<GenericUiContribution>(
        QStringLiteral("rob.ui.contributions"), QStringLiteral("机器人界面入口"), FormatDomain::Robotics,
        QList<UiAction>{
            {QStringLiteral("rob.plot.tf"), QStringLiteral("TF 树可视化"), QStringLiteral("menu.plot"),
             QString(), QString()}});
    pack->add<GenericDocContribution>(
        QStringLiteral("rob.docs"), QStringLiteral("机器人知识文档"), FormatDomain::Robotics,
        QList<DocEntry>{
            {QStringLiteral("rob.doc.mcap"), QStringLiteral("MCAP / ROSbag2（待接入）"),
             QStringLiteral("docs/robotics/mcap.md"), QStringLiteral("text/markdown")}});
    return pack;
}

static std::unique_ptr<ToolPack> buildGnssPack()
{
    auto pack = std::make_unique<ToolPack>(QStringLiteral("pack.gnss"),
                                           QStringLiteral("卫导工具箱"), FormatDomain::Gnss);
    pack->add<GenericScriptContribution>(
        QStringLiteral("gnss.script.helpers"), QStringLiteral("卫导脚本库"), FormatDomain::Gnss,
        QStringLiteral("lua"),
        QStringLiteral("ps.gnss = { quality = function() ps.log('gnss quality helpers') end }\n"),
        QStringList{QStringLiteral("ps.gnss")});
    pack->add<GenericDocContribution>(
        QStringLiteral("gnss.docs"), QStringLiteral("卫导知识文档"), FormatDomain::Gnss,
        QList<DocEntry>{
            {QStringLiteral("gnss.doc.nmea"), QStringLiteral("NMEA / UBX / OEM7（待接入）"),
             QStringLiteral("docs/gnss/nmea.md"), QStringLiteral("text/markdown")}});
    return pack;
}

void registerBuiltinPacks(ExtensionRegistry &registry)
{
    registry.registerPack(buildCommonPack());
    registry.registerPack(buildAutopilotPack());
    registry.registerPack(buildAutomotivePack());
    registry.registerPack(buildRoboticsPack());
    registry.registerPack(buildGnssPack());
}

} // namespace ng::ext
