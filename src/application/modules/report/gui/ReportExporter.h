#pragma once

// 报告导出器：多画布 + 元数据 → PDF。
//
// 归属 report 模块呈现层：报告内容是报告域业务（模板 + 数据集元数据），
// 画布经 AppContext 注入的 canvas provider 取得，模块不反向依赖外壳。
// 组合根注册 ReportExportHost（实现 IReportExportHost），由 report 模块在
// initialize 安装、onDetach 卸载；install/uninstall 幂等。
//
// 矢量管线：QPdfWriter（QtGui）+ QWidget::render(&painter) —— 视图走
// QPainter 绘制，文字与线条直接以矢量写进 PDF，验收"不糊"免费。
// 中文字体走 Qt 字体子系统（嵌 TrueType 子集），无需手工嵌字体文件。

#include "IReportExportHost.h"
#include "Result.h"

#include <QString>

class QWidget;

namespace nextpilot::plotscope::context {
class AppContext;
}

namespace nextpilot::plotscope::gui {

// 一次导出请求（纯数据）。path 为空 → 由调用方先选好；templateId 空 → 内置默认模板。
struct ReportRequest
{
    QString path;       // 输出 PDF 路径
    QString title;      // 封面标题（空 = 调用方按产品 spec 兜底）
    QString creator;    // PDF creator 元数据（产品显示名，空 = 不写）
    QString subtitle;   // 封面副标题/摘要（可空）
    QString notes;      // 结论/备注文本（可空）
    QString templateId; // 工具箱贡献的模板 id（空 = 内置默认）
};

// 执行导出。画布与标题经 appCtx 注入的 canvas provider 取得（外壳装配时注入）；
// 实现按通用 QWidget 取第一个有真实尺寸的页签渲染，不 qobject_cast 任何模块呈现层类型。
nextpilot::plotscope::foundation::Result<void> exportReportPdf(nextpilot::plotscope::context::AppContext &appCtx,
                                                               const ReportRequest &request);

// report.export 的安装/卸载实现：注册进交互表，handler 与脚本、Ribbon 共用。
// 生命周期由组合根（main / 测试）负责：host 必须在 App 之前析构完成——顺序反了，
// ~App 的 deactivateAll 会对着已析构的 host 做虚调用（Debug 下 pure virtual abort）。
class ReportExportHost final : public nextpilot::plotscope::report::IReportExportHost
{
public:
    explicit ReportExportHost(nextpilot::plotscope::context::AppContext &appCtx) : m_appCtx(appCtx) {}
    void install() override;
    void uninstall() override;

private:
    nextpilot::plotscope::context::AppContext &m_appCtx;
    bool m_installed = false;
};

} // namespace nextpilot::plotscope::gui
