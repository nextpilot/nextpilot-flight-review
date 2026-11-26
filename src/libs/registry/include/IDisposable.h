#pragma once

// A8b-M3-a 第 2 笔（registry 层，对位 IActionProvider）：可撤销注册句柄（vscode.Disposable 对位）。
// registerEditorProvider 等运行层注册返回本接口；dispose() 即撤销该条注册，
// owner 卸载时宿主整批撤（unmount 语义，见 ViewRegistry::unregisterOwner）。
// 纯契约，无 Qt 依赖，headless 可用。

namespace nextpilot::plotscope::registry
{

class IDisposable
{
public:
    virtual ~IDisposable() = default;
    // 撤销本次注册；幂等（二次调用无效果）。
    virtual void dispose() = 0;
};

} // namespace nextpilot::plotscope::registry
