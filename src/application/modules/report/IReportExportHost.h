#pragma once

// report.export 的安装/卸载口。
//
// 模块只认接口；实现在 gui/（导出管线要 Qt Widgets），由组合根注册进
// InstantiationService。headless 没有实现，模块取不到时跳过安装。
// install / uninstall 必须幂等：重复安装只保留一份 viewAction。

namespace nextpilot::plotscope::report {

class IReportExportHost
{
public:
    virtual ~IReportExportHost() = default;
    virtual void install() = 0;
    virtual void uninstall() = 0;
};

} // namespace nextpilot::plotscope::report
