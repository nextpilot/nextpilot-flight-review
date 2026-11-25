#pragma once

// 报告模块：只声明 Ribbon「报告」页入口（纯数据
// UiAction），导出管线在外壳层（gui/shell/ReportExporter）——外壳按 action id
// 特判弹保存对话框（与协议编辑器同一模式）；脚本走 app.call("report.export")
// view action 同路径。本模块零 Qt Widgets 依赖，headless 构建保持纯净。
//
// 写法对齐既有模块（ScriptModule/ProtocolModule）：全局作用域 + using 别名。

#include "IWorkbenchContribution.h"

#include <QObject>

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
    void onAttach(nextpilot::plotscope::host::IUiContext &ui) override;
    nextpilot::plotscope::registry::ModuleContributions contributes() const override;
};
