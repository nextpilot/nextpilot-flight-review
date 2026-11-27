#pragma once

// 报告模块：只声明 Ribbon「报告」页入口（纯数据 UiAction），导出管线在
// gui/ReportExporter。report.export 的 install/uninstall 经 IReportExportHost：
// 组合根注册实现，本模块在 initialize 安装、onDetach 卸载，重激活自动恢复。
// 本模块零 Qt Widgets 依赖，headless 构建保持纯净。
//
// 写法对齐既有模块（ScriptModule/ProtocolModule）：全局作用域 + using 别名。

#include "IWorkbenchContribution.h"

#include <QObject>

namespace nextpilot::plotscope::report {
class IReportExportHost;
} // namespace nextpilot::plotscope::report

using namespace nextpilot::plotscope::host;
using namespace nextpilot::plotscope::registry;

class ReportModule final : public nextpilot::plotscope::host::IWorkbenchContribution
{
    Q_OBJECT
public:
    static nextpilot::plotscope::host::ModuleInfo moduleInfo();
    static nextpilot::plotscope::host::IWorkbenchContribution *create();

    nextpilot::plotscope::host::ModuleInfo info() const override;
    void initialize(nextpilot::plotscope::instantiation::InstantiationService &services) override;
    // 卸载时摘除 report.export；重激活由 initialize 重新安装。
    void onDetach() override;
    nextpilot::plotscope::registry::ModuleContributions contributes() const override;

private:
    nextpilot::plotscope::report::IReportExportHost *m_host = nullptr;
};
