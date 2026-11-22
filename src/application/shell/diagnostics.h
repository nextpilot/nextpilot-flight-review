#pragma once

// 应用诊断收集（--dump-diagnostics 共用实现）。
// CLI 与 GUI 都能调：输出一份 JSON，覆盖模块/扩展包/数据集/意图工具目录，
// 供 CI 验收与运维排查。只读，不改变应用状态。

#include <QJsonObject>

namespace nextpilot::plotscope::app
{
class App;
}

namespace nextpilot::plotscope::app::diag
{

// 收集诊断快照。host 必须已 startup（模块/扩展包已装载）。
QJsonObject collect(App &application);

} // namespace nextpilot::plotscope::app::diag
