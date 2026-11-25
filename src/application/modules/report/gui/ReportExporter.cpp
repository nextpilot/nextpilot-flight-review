#include "ReportExporter.h"

#include "AppContext.h"
#include "Contribution.h"
#include "Dataset.h"
#include "DatasetHub.h"
#include "ErrorCodes.h"
#include "InteractionHub.h"
#include "DialogLauncherHub.h"
#include "ProductSpec.h"
#include "Result.h"

#include <QDateTime>
#include <QFileDialog>
#include <QFont>
#include <QList>
#include <QPageLayout>
#include <QPagedPaintDevice>
#include <QPainter>
#include <QPdfWriter>
#include <QRectF>
#include <QStringList>
#include <QVariant>
#include <QWidget>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;

namespace nextpilot::plotscope::gui {

using nextpilot::plotscope::context::AppContext;

namespace {
// 元数据行（键 → 值），metadata 节渲染成两列表格。
QStringList metadataLines(AppContext &appCtx)
{
    QStringList out;
    out << QStringLiteral("导出时间: %1")
               .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));

    auto &repo = appCtx.data();
    out << QStringLiteral("数据集: %1 个").arg(repo.count());
    for (DatasetId id : repo.ids())
    {
        const Dataset *ds = repo.get(id);
        if (!ds)
            continue;
        QStringList tables;
        for (int i = 0; i < ds->tableCount() && i < 6; ++i)
        {
            const auto *t = ds->table(i);
            if (t)
                tables << QStringLiteral("%1(%2行)").arg(t->name).arg(t->rows);
        }
        out << QStringLiteral("  #%1 %2: %3%4")
                   .arg(id)
                   .arg(ds->name())
                   .arg(tables.join(QStringLiteral(", ")))
                   .arg(ds->tableCount() > 6 ? QStringLiteral(" …") : QString());
    }
    return out;
}

// 写一页标题（居中大字），返回下一页。
void writeTitlePage(QPdfWriter &writer, QPainter &painter, const QString &title, const QString &subtitle)
{
    QRectF page(0, 0, writer.width(), writer.height());
    painter.save();
    painter.resetTransform();

    QFont f = painter.font();
    f.setPixelSize(int(writer.height() * 0.05));
    f.setBold(true);
    painter.setFont(f);
    painter.drawText(QRectF(page.left(), page.height() * 0.4, page.width(), page.height() * 0.08), Qt::AlignCenter,
                     title);

    if (!subtitle.isEmpty())
    {
        f.setPixelSize(int(writer.height() * 0.028));
        f.setBold(false);
        painter.setFont(f);
        painter.setPen(QColor(0x66, 0x66, 0x66));
        painter.drawText(QRectF(page.left(), page.height() * 0.5, page.width(), page.height() * 0.4),
                         Qt::AlignHCenter | Qt::TextWordWrap, subtitle);
    }
    painter.restore();
    writer.newPage();
}

// 写一节：节标题 + 键值行（元数据/文本共用；文本按整段 wrap）。
void writeTextSection(QPdfWriter &writer, QPainter &painter, const QString &title, const QStringList &lines, qreal y,
                      QRectF page)
{
    QFont f = painter.font();
    f.setPixelSize(int(writer.height() * 0.030));
    f.setBold(true);
    painter.setFont(f);
    painter.drawText(QRectF(page.left(), y, page.width(), page.height() * 0.04), Qt::AlignLeft, title);
    y += page.height() * 0.05;

    f.setPixelSize(int(writer.height() * 0.022));
    f.setBold(false);
    painter.setFont(f);
    for (const QString &line : lines)
    {
        QRectF used;
        painter.drawText(QRectF(page.left(), y, page.width(), page.height() - y - page.height() * 0.06),
                         Qt::TextWordWrap, line, &used);
        y = used.bottom() + painter.fontMetrics().height() * 0.6;
        if (y > page.height() * 0.94)
        {
            writer.newPage();
            painter.resetTransform();
            y = page.height() * 0.08;
        }
    }
}

} // namespace

Result<void> exportReportPdf(AppContext &appCtx, const ReportRequest &request)
{
    if (request.path.isEmpty())
        return fail(err::Args, QStringLiteral("未指定输出路径"));

    // A0b 清场：外壳注入画布的扩权接缝已按施工令删除。
    // 报告不设画布 provider、不借 plot 控件；最终画布统一走 uischema schema，
    // 由 libs 的 canvas 渲染器（底层 fastplot）出图——见 A7b（IKXXED，画布走 uischema）。
    // 当前 A0b 阶段为 headless 口径——无画布（Q4：报告仍可导出元数据/文字，"没数据"可接受）。
    const QList<QWidget *> canvases;
    const QStringList titles;

    // 模板：优先用工具箱贡献的（按 templateId，经 main 已有的 toolbox() 直连），否则
    // 内置默认（封面/元数据/画布/结论）。
    QList<nextpilot::plotscope::registry::ReportSection> sections;
    if (!request.templateId.isEmpty())
    {
        const auto *tpl = appCtx.toolbox().reports.find(request.templateId);
        if (!tpl)
            return fail(err::NoEnt, QStringLiteral("报告模板不存在: %1").arg(request.templateId));
        sections = tpl->sections;
    }
    else
    {
        sections = {
            {QStringLiteral("title"), {}, request.subtitle},
            {QStringLiteral("metadata"), QStringLiteral("概要"), {}},
            {QStringLiteral("canvas"), {}, {}},
            {QStringLiteral("text"), QStringLiteral("结论"), request.notes},
        };
    }

    // 选第一个有真实尺寸的中央页签作为画布：通用 QWidget 路径，不 qobject_cast
    // 任何模块呈现层类型。注意这是 A0b 的过渡形态；A7b（IKXXED）将把画布统一为
    // uischema schema + libs 渲染器，报告不设 provider、不借 plot 活控件，此处随之下线。
    QWidget *canvas = nullptr;
    for (QWidget *w : canvases)
    {
        if (w && w->width() > 1 && w->height() > 1)
        {
            canvas = w;
            break;
        }
    }
    const bool hasCanvas = canvas != nullptr;

    QPdfWriter writer(request.path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(18, 18, 18, 18), QPageLayout::Millimeter);
    writer.setResolution(96);
    writer.setTitle(request.title);
    writer.setCreator(request.creator);

    QPainter painter(&writer);
    if (!painter.isActive())
        return fail(err::Io, QStringLiteral("PDF 打开失败: %1").arg(request.path));
    painter.setRenderHint(QPainter::Antialiasing);
    // 中文字体：默认字体走 Qt 字体子系统（无字体回退时才乱码），显式选常见字体。
    QFont base = painter.font();
    base.setFamilies({QStringLiteral("Noto Sans CJK SC"), QStringLiteral("WenQuanYi Zen Hei"),
                      QStringLiteral("Microsoft YaHei"), QStringLiteral("PingFang SC")});
    painter.setFont(base);

    const QRectF page(0, 0, writer.width(), writer.height());
    qreal y = page.height() * 0.08;

    for (const auto &sec : sections)
    {
        if (sec.kind == QStringLiteral("title"))
        {
            writeTitlePage(writer, painter, request.title, sec.content.isEmpty() ? request.subtitle : sec.content);
            y = page.height() * 0.08;
        }
        else if (sec.kind == QStringLiteral("metadata"))
        {
            writeTextSection(writer, painter, sec.title.isEmpty() ? QStringLiteral("概要") : sec.title,
                             metadataLines(appCtx), y, page);
            y = page.height() * 0.55;
        }
        else if (sec.kind == QStringLiteral("canvas"))
        {
            if (!hasCanvas)
                continue;
            // 节标题 = 页签名（titles 与 canvases 一一对应）。
            QString secTitle = QStringLiteral("画布");
            for (int i = 0; i < canvases.size(); ++i)
                if (canvases.at(i) == canvas && i < titles.size() && !titles.at(i).isEmpty())
                    secTitle = titles.at(i);

            // 矢量渲染：widget->render 进 QPainter，线条/文字按矢量写入 PDF。
            writer.newPage();
            painter.resetTransform();
            QFont f = painter.font();
            f.setPixelSize(int(writer.height() * 0.028));
            f.setBold(true);
            painter.setFont(f);
            painter.drawText(QRectF(page.left(), page.height() * 0.04, page.width(), page.height() * 0.04),
                             Qt::AlignLeft, secTitle);

            const QRectF body(page.left(), page.height() * 0.10, page.width(), page.height() * 0.84);
            const QSizeF ws(canvas->width(), canvas->height());
            if (ws.width() > 1 && ws.height() > 1)
            {
                // 等比缩放居中（画布纵横比通常与 A4 不同）。
                const qreal scale = qMin(body.width() / ws.width(), body.height() / ws.height());
                const qreal w = ws.width() * scale, h = ws.height() * scale;
                painter.save();
                painter.translate(body.left() + (body.width() - w) / 2, body.top() + (body.height() - h) / 2);
                painter.scale(scale, scale);
                canvas->render(&painter, QPoint(0, 0), QRegion(0, 0, canvas->width(), canvas->height()),
                               QWidget::DrawChildren | QWidget::DrawWindowBackground);
                painter.restore();
            }
            y = page.height() * 0.96;
        }
        else if (sec.kind == QStringLiteral("text"))
        {
            if (sec.content.isEmpty() && request.notes.isEmpty())
                continue;
            const QString body = sec.content.isEmpty() ? request.notes : sec.content;
            writeTextSection(writer, painter, sec.title.isEmpty() ? QStringLiteral("备注") : sec.title,
                             body.split(QLatin1Char('\n')), y, page);
            y = page.height() * 0.96;
        }
    }
    painter.end();

    return Result<void>();
}

// 装配层调用：把 report.export viewAction 注册到 InteractionHub。
// 脚本 app.call("report.export", {...}) 与 Ribbon 按钮都走这条 handler。
// title/creator 由外壳按 ProductSpec 兜底后经 params 传入——模块不认识产品名。
void installReportExport(nextpilot::plotscope::context::AppContext &appCtx)
{
    appCtx.interactions().registerViewAction(
        QStringLiteral("report.export"),
        [&appCtx](const QVariantMap &params) -> nextpilot::plotscope::foundation::Result<QVariant> {
            ReportRequest req;
            req.path = params.value(QStringLiteral("path")).toString();
            const auto spec = nextpilot::plotscope::registry::plotScopeProductSpec();
            req.title = params.value(QStringLiteral("title")).toString();
            if (req.title.isEmpty())
                req.title = spec.reportTitle.isEmpty() ? spec.displayName : spec.reportTitle;
            req.creator = params.value(QStringLiteral("creator")).toString();
            if (req.creator.isEmpty())
                req.creator = spec.displayName;
            req.subtitle = params.value(QStringLiteral("subtitle")).toString();
            req.notes = params.value(QStringLiteral("notes")).toString();
            req.templateId = params.value(QStringLiteral("template")).toString();
            const auto r = exportReportPdf(appCtx, req);
            if (!r)
                return nextpilot::plotscope::foundation::Result<QVariant>(r.error());
            return nextpilot::plotscope::foundation::Result<QVariant>(QVariant(req.path));
        });
}

// A2a：Ribbon「导出报告」的 GUI 交互（选 PDF 路径）归模块自己，外壳零业务特判。
// 只弹路径，title/creator 由 report.export handler 用 ProductSpec 兜底。
namespace {
void launchReportExport(QWidget *parent, nextpilot::plotscope::context::AppContext &appCtx,
                        nextpilot::plotscope::intent::IIntentExecutor &executor)
{
    Q_UNUSED(executor);
    const auto spec = nextpilot::plotscope::registry::plotScopeProductSpec();
    const QString path = QFileDialog::getSaveFileName(parent, QStringLiteral("导出报告"), QStringLiteral("report.pdf"),
                                                      QStringLiteral("PDF (*.pdf);;全部 (*)"));
    if (path.isEmpty())
        return;
    QVariantMap params;
    params.insert(QStringLiteral("path"), path);
    QVariant out;
    Result<QVariant> res(out);
    appCtx.interactions().dispatchViewAction(QStringLiteral("report.export"), params, res);
}
} // namespace

PS_REGISTER_DIALOG_LAUNCHER("report.workbench.export", reportExport, launchReportExport)

} // namespace nextpilot::plotscope::gui
