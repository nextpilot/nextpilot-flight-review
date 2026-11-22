#pragma once

// 报告导出器：多画布 + 元数据 → PDF。
//
// 归属外壳层（shell/）的理由：它需要 ShellBinder 的中央区页签枚举
// （centralWidgets/centralTitles），与协议编辑器等"外壳按 id 特判弹出的
// GUI 能力"同一层——业务语义（报告内容）仍来自模板与数据集，本类只做拼版。
//
// 矢量管线：QPdfWriter（QtGui）+ QWidget::render(&painter) —— PlotCanvas/
// fastplot 走 QPainter 绘制，文字与线条直接以矢量写进 PDF，验收"不糊"免费。
// 中文字体走 Qt 字体子系统（嵌 TrueType 子集），无需手工嵌字体文件。

#include "Result.h"

#include <QString>

class QWidget;

namespace nextpilot::plotscope::context
{
class AppContext;
}

namespace nextpilot::plotscope::gui
{

// 一次导出请求（纯数据）。path 为空 → 由调用方先选好；templateId 空 → 内置默认模板。
struct ReportRequest
{
    QString path;        // 输出 PDF 路径
    QString title;       // 封面标题（空 = "PlotScope 报告"）
    QString subtitle;    // 封面副标题/摘要（可空）
    QString notes;       // 结论/备注文本（可空）
    QString templateId;  // 工具箱贡献的模板 id（空 = 内置默认）
};

// 执行导出。canvases = 中央画布区页签内容（与 titles 一一对应）；实现里只取
// CanvasWorkspace（qobject_cast，同库直链）——脚本编辑器等非画布页签自动跳过。
nextpilot::plotscope::foundation::Result<void> exportReportPdf(
    nextpilot::plotscope::context::AppContext &appCtx,
    const QList<QWidget *> &canvases, const QStringList &titles,
    const ReportRequest &request);

} // namespace nextpilot::plotscope::gui
