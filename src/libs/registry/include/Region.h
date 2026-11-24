#pragma once

// 逻辑区域枚举（契约 D14 / D17）。
//
// 区域是壳的**代码常量**，不再是 "dock.left" / "canvas.center" 这类方位字符串：
//   - 进程内 C++ 一律传 `Region` 枚举，拼错在编译期就抓出来；
//   - `toRegion()` / `regionName()` 只在 JSON / IPC / alias 边界做一次解析与序列化，
//     不在模块、注册表、绑定器之间来回传字符串；
//   - 不设 `isKnownArea()` 这类运行期校验：枚举是编译错误，不是日志警告。
//
// 区域没有名字（label）："数据集" / "属性" 这类展示名属于绑定到该区域的**视图**，
// 不属于区域本身（D17）。

#include <QString>

namespace nextpilot::plotscope::registry
{

enum class Region
{
    ActivityBar,       // Activity Bar 竖条（活动项容器）
    PrimarySidebar,    // 主侧栏（左栏，视图 section 栈）
    SecondarySidebar,  // 辅助栏（右栏，属性等并列视图）
    Panel,             // 底部面板条（终端 / 输出 / 问题 / 诊断 / 调试控制台）
    Editor,            // 中央编辑器区（画布、脚本编辑器、表格等页签）
};

// 规范名（与 VS Code 逻辑位置对位）：
// activitybar / primarySidebar / secondarySidebar / panel / editor。
QString regionName(Region region);

// 边界解析：接受规范名，以及下表列出的一次性 alias（旧 dock.*/canvas.* 方位名）。
//
// alias 是**一次性迁移映射**，不是长期双轨：新代码一律写枚举；只有读旧持久化、
// JSON 脚本、IPC 报文时才经过这里。未知名兜底为 `Region::Editor`。
//
// 旧名去向：
//   dock.left        → PrimarySidebar
//   dock.right       → SecondarySidebar
//   dock.bottom      → Panel
//   dock.diagnostics → Panel（诊断并入底部面板组，不单独占区）
//   canvas.center    → Editor
//   canvas.tool      → 取消：工具归 Editor 视图的 view/title（ViewTitleAction），
//                      不再是独立区域；边界解析兜底到 Editor。
Region toRegion(const QString &name);

} // namespace nextpilot::plotscope::registry
