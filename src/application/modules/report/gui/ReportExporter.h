#pragma once

// 报告导出器：多画布 + 元数据 → PDF。
//
// 归属 report 模块呈现层：报告内容是报告域业务（模板 + 数据集元数据），
// 画布经 AppContext 注入的 canvas provider 取得，模块不反向依赖外壳。
// 装配层调 installReportExport 把 "report.export" viewAction 注册到交互表。
//
// 矢量管线：QPdfWriter（QtGui）+ QWidget::render(&painter) —— 视图走
// QPainter 绘制，文字与线条直接以矢量写进 PDF，验收"不糊"免费。
// 中文字体走 Qt 字体子系统（嵌 TrueType 子集），无需手工嵌字体文件。

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

// 装配层调用：注册 "report.export" viewAction（脚本与 Ribbon 按钮共用入口）。
void installReportExport(nextpilot::plotscope::context::AppContext &appCtx);

} // namespace nextpilot::plotscope::gui
