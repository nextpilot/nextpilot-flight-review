#include "ReportModule.h"

#include "BuiltinModuleHub.h"
#include "Contribution.h"
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

IWorkbenchContribution *ReportModule::create()
{
    return new ReportModule();
}

void ReportModule::initialize(InstantiationService &)
{
    // 报告模块无需运行时装配：入口声明在 onAttach，导出管线在外壳层。
}

void ReportModule::onAttach(IUiContext &ui)
{
    // GUI 环境下才走到这里（headless 时宿主不调用 onAttach）。
    //
    // 「报告」页 = "导出"组：一键导出 PDF（多画布 + 元数据 + 结论）。
    // action id 由外壳特判（弹保存路径 → QPdfWriter 矢量管线）；脚本侧
    // app.call("report.export", {...}) 走同一管线（MainWindow 注册 view action）。
    ui.declareRibbonPage(QStringLiteral("ribbon.page.report"), QStringLiteral("报告"), 7);
    ui.declareRibbonGroup(QStringLiteral("ribbon.group.report.export"), QStringLiteral("导出"),
                          QStringLiteral("ribbon.page.report"), 0);

    UiAction a;
    a.id = QStringLiteral("report.workbench.export");
    a.label = QStringLiteral("导出报告");
    a.slotId = QStringLiteral("ribbon.group.report.export");
    a.icon = QStringLiteral("file_save");
    a.large = true;
    a.order = 0;
    ui.addAction(a);
}

#include "ReportModule.moc"

// 编译期自注册：新增报告模块无需改 App / 宿主。
PS_REGISTER_MODULE(ReportModule)
