#include "ReportModule.h"

#include "BuiltinModuleHub.h"
#include "Contribution.h"
#include "IReportExportHost.h"
#include "InstantiationService.h"

using nextpilot::plotscope::instantiation::InstantiationService;

ModuleInfo ReportModule::moduleInfo()
{
    return ModuleInfo{
        QStringLiteral("mod.report"),
        QStringLiteral("报告模块"),
        QStringLiteral("1.0"),
        {QStringLiteral("report.exporter")},
        {} // 无强依赖：导出内容来自模板与数据集，画布由外壳提供
    };
}

ModuleInfo ReportModule::info() const
{
    return moduleInfo();
}

IWorkbenchContribution *ReportModule::create() { return new ReportModule(); }

void ReportModule::initialize(InstantiationService &services)
{
    // headless 没有实现（导出要 Qt Widgets），取不到就跳过：report.export 不出现。
    m_host = services.getService<nextpilot::plotscope::report::IReportExportHost>(QStringLiteral("report.exportHost"));
    if (m_host)
        m_host->install();
}

ModuleContributions ReportModule::contributes() const
{
    // A8b-M2：声明式数据（单一通道）。「报告」页 = "导出"组：一键导出 PDF。
    using namespace nextpilot::plotscope::registry;
    ModuleContributions c;
    c.owner = QStringLiteral("report");
    c.ribbonPages.append(RibbonPageDecl{QStringLiteral("ribbon.page.report"),
                                        QStringLiteral("报告"), 7});
    c.ribbonGroups.append(RibbonGroupDecl{QStringLiteral("ribbon.group.report.export"),
                                          QStringLiteral("导出"),
                                          QStringLiteral("ribbon.page.report"), 0});
    UiAction a;
    a.id = QStringLiteral("report.workbench.export");
    a.label = QStringLiteral("导出报告");
    a.slotId = QStringLiteral("ribbon.group.report.export");
    a.icon = QStringLiteral("file_save");
    a.large = true;
    a.order = 0;
    c.actions.append(a);
    return c;
}

void ReportModule::onDetach()
{
    if (m_host)
        m_host->uninstall();
}

#include "ReportModule.moc"

// 编译期自注册：新增报告模块无需改 App / 宿主。
PS_REGISTER_MODULE(ReportModule)
