#include "AutopilotReview.h"

#include "MainWindow.h"
#include "ModuleBridge.h"
#include "ps_kernel.h"

using ps::components::HostContext;

#include <QDateTime>
#include <QWidget>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QSet>
#include <cstdint>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>

#include <cmath>
#include <limits>

namespace
{

struct FlightMeta
{
    QString profile;
    QString source;
    QString path;
    int channelCount = 0;
    double duration = 0.0;
};

QString lower(const QString &text)
{
    return text.toLower();
}

bool anyToken(const QString &name, const QStringList &tokens)
{
    for (const QString &token : tokens)
        if (name.contains(token, Qt::CaseInsensitive))
            return true;
    return false;
}

QString detectProfile(const ps::bridge::SourceView &view, const QString &path)
{
    QString joined;
    for (const PsChannelInfo *channel : view.channels)
        if (channel && channel->name)
            joined += lower(QString::fromUtf8(channel->name)) + QLatin1Char(' ');
    if (joined.contains(QStringLiteral("vehicle_")) || joined.contains(QStringLiteral("px4")))
        return QStringLiteral("PX4");
    if (joined.contains(QStringLiteral("ardupilot")) || joined.contains(QStringLiteral("apm")) ||
        lower(path).endsWith(QStringLiteral(".bin")))
        return QStringLiteral("ArduPilot");
    return QStringLiteral("Generic");
}

const ps::bridge::SourceView *activeView(MainWindow *mw)
{
    const QVector<ps::bridge::SourceView> views = ps::bridge::sourceViews(mw);
    if (views.isEmpty())
        return nullptr;
    // 数据源列表按最近加载排列，分析默认取最近一个源。
    static ps::bridge::SourceView current;
    current = views.constLast();
    return &current;
}

QVector<const PsChannelInfo *> pickGroup(const ps::bridge::SourceView &view,
                                         const QStringList &tokens, int limit,
                                         QSet<QString> &used)
{
    QVector<const PsChannelInfo *> out;
    for (const PsChannelInfo *channel : view.channels)
    {
        if (!channel || !channel->name || out.size() >= limit)
            continue;
        const QString name = QString::fromUtf8(channel->name);
        if (used.contains(name) || !anyToken(name, tokens))
            continue;
        used.insert(name);
        out.append(channel);
    }
    return out;
}

void createDefaultPages(MainWindow *mw, const ps::bridge::SourceView &view)
{
    QSet<QString> used;
    struct Group
    {
        const char *title;
        QStringList tokens;
        int limit;
    };
    const Group groups[] = {
        {"姿态", {"attitude", "att.", "roll", "pitch", "yaw"}, 4},
        {"位置与速度", {"position", "local_position", "global_position", "alt", "vx", "vy", "vz"}, 6},
        {"电源", {"battery", "power", "voltage", "current", "remaining"}, 4},
        {"状态与 EKF", {"status", "armed", "mode", "ekf", "failsafe"}, 6},
        {"遥控与执行器", {"rc_channels", "rc.", "throttle", "actuator", "servo"}, 6},
    };

    bool made = false;
    for (const Group &group : groups)
    {
        const QVector<const PsChannelInfo *> channels =
            pickGroup(view, group.tokens, group.limit, used);
        if (channels.isEmpty())
            continue;
        ps::bridge::newPage(mw);
        PlottingCanvas *canvas = ps::bridge::currentCanvas(mw);
        if (!canvas)
            continue;
        ps::bridge::canvasSetChannels(canvas, channels);
        ps::bridge::canvasSetTitle(canvas, QString::fromUtf8(group.title));
        made = true;
    }

    if (!made)
    {
        QVector<const PsChannelInfo *> channels;
        for (const PsChannelInfo *channel : view.channels)
        {
            if (channel && channel->name && channels.size() < 8)
                channels.append(channel);
        }
        if (!channels.isEmpty())
        {
            ps::bridge::newPage(mw);
            if (PlottingCanvas *canvas = ps::bridge::currentCanvas(mw))
            {
                ps::bridge::canvasSetChannels(canvas, channels);
                ps::bridge::canvasSetTitle(canvas, QStringLiteral("默认曲线"));
            }
        }
    }
}

QString htmlEscape(QString text)
{
    text.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    text.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    text.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    text.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return text;
}

FlightMeta collectMeta(const ps::bridge::SourceView &view)
{
    FlightMeta meta;
    meta.profile = detectProfile(view, view.path);
    meta.source = view.alias.isEmpty() ? view.name : view.alias;
    meta.path = view.path;
    meta.channelCount = view.channels.size();
    double first = std::numeric_limits<double>::infinity();
    double last = -std::numeric_limits<double>::infinity();
    for (const PsChannelInfo *channel : view.channels)
    {
        if (!channel || channel->point_count == 0)
            continue;
        first = std::min(first, channel->time[0]);
        last = std::max(last, channel->time[channel->point_count - 1]);
    }
    if (std::isfinite(first) && std::isfinite(last))
        meta.duration = qMax(0.0, last - first);
    return meta;
}

QString buildReport(const ps::bridge::SourceView &view, const FlightMeta &meta)
{
    struct Row
    {
        QString name;
        QString unit;
        uint32_t count = 0;
        double min = 0.0;
        double max = 0.0;
        double mean = 0.0;
        double last = 0.0;
    };
    QVector<Row> rows;
    for (const PsChannelInfo *channel : view.channels)
    {
        if (!channel || !channel->name)
            continue;
        Row row;
        row.name = QString::fromUtf8(channel->name);
        row.unit = channel->unit ? QString::fromUtf8(channel->unit) : QString();
        row.count = channel->point_count;
        if (row.count == 0)
        {
            rows.append(row);
            continue;
        }
        row.min = std::numeric_limits<double>::infinity();
        row.max = -std::numeric_limits<double>::infinity();
        double sum = 0.0;
        uint32_t valid = 0;
        for (uint32_t i = 0; i < row.count; ++i)
        {
            const double value = channel->value[i];
            if (!std::isfinite(value))
                continue;
            row.min = std::min(row.min, value);
            row.max = std::max(row.max, value);
            sum += value;
            ++valid;
        }
        row.last = channel->value[row.count - 1];
        row.mean = valid ? sum / valid : 0.0;
        if (valid == 0)
            row.min = row.max = 0.0;
        rows.append(row);
    }

    QString html;
    html += QStringLiteral("<!doctype html><html><head><meta charset=\"utf-8\">"
                           "<title>飞行分析报告</title><style>"
                           "body{font-family:-apple-system,Segoe UI,sans-serif;margin:32px;color:#20242a}"
                           "h1{font-size:26px;margin:0 0 8px}h2{font-size:18px;margin:28px 0 10px}"
                           ".meta{color:#667085;font-size:13px;line-height:1.7}"
                           "table{border-collapse:collapse;width:100%;font-size:13px}"
                           "th,td{border-bottom:1px solid #e7e9ee;padding:7px 8px;text-align:left}"
                           "th{color:#667085;font-weight:600}td.num{text-align:right;font-variant-numeric:tabular-nums}"
                           "</style></head><body>");
    html += QStringLiteral("<h1>%1 · 飞行分析</h1>").arg(htmlEscape(meta.source));
    html += QStringLiteral("<div class=\"meta\">机型/固件：%1<br>文件：%2<br>通道：%3 · 时长：%4 s · 生成：%5</div>")
                .arg(htmlEscape(meta.profile), htmlEscape(meta.path))
                .arg(meta.channelCount)
                .arg(meta.duration, 0, 'f', 2)
                .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
    html += QStringLiteral("<h2>通道统计</h2><table><thead><tr>"
                           "<th>通道</th><th>单位</th><th>点数</th><th>最小</th><th>最大</th><th>均值</th><th>末值</th>"
                           "</tr></thead><tbody>");
    for (const Row &row : rows)
    {
        html += QStringLiteral("<tr><td>%1</td><td>%2</td><td class=\"num\">%3</td>"
                              "<td class=\"num\">%4</td><td class=\"num\">%5</td>"
                              "<td class=\"num\">%6</td><td class=\"num\">%7</td></tr>")
                    .arg(htmlEscape(row.name), htmlEscape(row.unit))
                    .arg(row.count)
                    .arg(row.min, 0, 'g', 8)
                    .arg(row.max, 0, 'g', 8)
                    .arg(row.mean, 0, 'g', 8)
                    .arg(row.last, 0, 'g', 8);
    }
    html += QStringLiteral("</tbody></table></body></html>");
    return html;
}

void report(MainWindow *mw)
{
    const ps::bridge::SourceView *view = activeView(mw);
    if (!view || view->channels.isEmpty())
    {
        QMessageBox::information(mw, QStringLiteral("飞行分析报告"),
                                 QStringLiteral("当前没有可分析的数据源。"));
        return;
    }
    const FlightMeta meta = collectMeta(*view);
    const QString dirPath =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
        QStringLiteral("/PlotScope/reports");
    QDir().mkpath(dirPath);
    QString base = QFileInfo(view->path).completeBaseName();
    if (base.isEmpty())
        base = QStringLiteral("flight");
    const QString filePath = QDir(dirPath).filePath(base + QStringLiteral("_review.html"));
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        QMessageBox::warning(mw, QStringLiteral("飞行分析报告"),
                             QStringLiteral("无法写入报告：%1").arg(filePath));
        return;
    }
    QTextStream stream(&file);
    stream << buildReport(*view, meta);
    file.close();
    QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
    QMessageBox::information(mw, QStringLiteral("飞行分析报告"),
                             QStringLiteral("报告已生成：\n%1").arg(filePath));
}

void logMessage(const HostContext &host, const QString &text)
{
    if (host.log)
        host.log(text);
}

} // namespace

namespace ps::components
{

void registerAutopilotReview(Registry &registry, HostContext &host)
{
    registry.addAction(QStringLiteral("autopilot.review.default"), [host](const ActionContext &ctx) {
        auto *mw = qobject_cast<MainWindow *>(ctx.parent);
        if (!mw && ctx.parent)
            mw = qobject_cast<MainWindow *>(ctx.parent->window());
        if (!mw)
            return;
        const ps::bridge::SourceView *view = activeView(mw);
        if (!view || view->channels.isEmpty())
        {
            logMessage(host, QStringLiteral("飞控默认曲线：当前没有数据源"));
            return;
        }
        createDefaultPages(mw, *view);
        logMessage(host, QStringLiteral("飞控默认曲线已创建：%1").arg(view->name));
    });

    registry.addAction(QStringLiteral("autopilot.review.report"), [](const ActionContext &ctx) {
        auto *mw = qobject_cast<MainWindow *>(ctx.parent);
        if (!mw && ctx.parent)
            mw = qobject_cast<MainWindow *>(ctx.parent->window());
        if (mw)
            report(mw);
    });

}

} // namespace ps::components
