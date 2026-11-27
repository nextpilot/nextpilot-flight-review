#include "ReportModule.h"

#include "BuiltinModuleHub.h"
#include "Contribution.h"
#include "IScriptCapabilities.h"
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
    // 报告模块无运行时装配项；只需拿到能力接口，供 onDetach 摘除由装配层注册的
    // report.export（注册点在外壳 ExportReportExporter，注销权按模块生命周期归位）。
    m_caps =
        services.getService<nextpilot::plotscope::uischema::IScriptCapabilities>(QStringLiteral("script.capabilities"));
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

void ReportModule::onAttach(IUiContext &ui)
{
    Q_UNUSED(ui);
    // A8b-M2：UI 声明已迁至 contributes()，宿主统一灌回；onAttach 不再声明。
}

void ReportModule::onDetach()
{
    if (m_caps)
        m_caps->unregisterViewAction(QStringLiteral("report.export"));
}

#include "ReportModule.moc"

// 编译期自注册：新增报告模块无需改 App / 宿主。
PS_REGISTER_MODULE(ReportModule)
