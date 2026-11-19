#pragma once

#include <QString>

namespace nextpilot::plotscope::registry
{

// 工具箱的业务领域分类。
//
// 工具箱按 PlotScope 服务的业务方向组织——这是它的分类学：
//   - 飞控 / 汽车 / 机器人 / 卫导 是四个核心方向；
//   - 通用（CSV/JSON/binary）与视频（同步）是横切方向。
// 每个格式驱动声明自己属于哪个领域；注册表据此分组、CLI 据此分栏展示。
// 新增业务方向只加一个枚举值，不动注册表与器官。
enum class Domain
{
    General,     // 通用：CSV / JSON / binary
    Autopilot,   // 飞控：ULog / apbin / tlog / mavlink / sim
    Automotive,  // 汽车：ASC / BLF / MDF4 / CAN(DBC)
    Robotics,    // 机器人：MCAP / ROSbag2
    Gnss,        // 卫导：NMEA / UBX / OEM7
    Video        // 视频：同步（非数据表格，预留）
};

// 稳定标识（序列化/配置用）与展示名。
const char *domainKey(Domain domain);
QString domainName(Domain domain);

} // namespace nextpilot::plotscope::registry
// B7 过渡别名：core 消亡前全仓仍以 core::Xxx 取用。簇 8 删 core 时移除。
namespace nextpilot::plotscope::core
{
using registry::Domain;
using registry::domainKey;
using registry::domainName;
} // namespace nextpilot::plotscope::core

