#pragma once

// 通道拖拽的线协议：数据树与画布之间唯一的"约定"。
//
// 为什么不传裸指针：
//   · 指针跨视图传递，生命周期无人担保，视图析构后就是悬垂；
//   · 指针无法序列化，脚本/回放/跨进程一律失效；
//   · 它把"绘图视图"和"数据源内部结构"焊死，违反星形解耦。
// 传稳定的**逻辑标识**三元组（datasetId + table + column）——
// 画布拿到后发 plot.channel 意图取数，与双击走完全相同的路径。
//
// 视图之间仍互不 include：数据树只 encode，画布只 decode，
// 两者唯一共享的是这张无依赖的小头文件（c6 下沉 libs/dataset，纯数据协议，无 Widgets）。

#include "Dataset.h"

#include <QMimeData>
#include <QString>

namespace nextpilot::plotscope::dataset
{
namespace channel_mime
{

// 自定义 MIME 类型（Qt 约定：application/x-<vendor>-<type>）。
inline const char *type() { return "application/x-ngplotscope-channel"; }

inline QString encode(nextpilot::plotscope::dataset::DatasetId datasetId, const QString &table, const QString &column)
{
    // 格式： "datasetId|table|column"，表名/列名取自 schema，不含 '|'（parser 已清洗）。
    return QString::number(datasetId) + QLatin1Char('|') + table + QLatin1Char('|') + column;
}

struct Decoded
{
    bool valid = false;
    nextpilot::plotscope::dataset::DatasetId datasetId = nextpilot::plotscope::dataset::kInvalidDataset;
    QString table;
    QString column;
};

inline Decoded decode(const QMimeData *mime)
{
    Decoded out;
    if (!mime || !mime->hasFormat(QString::fromLatin1(type())))
        return out;
    const QString text = QString::fromUtf8(mime->data(QString::fromLatin1(type())));
    const QStringList parts = text.split(QLatin1Char('|'));
    if (parts.size() != 3)
        return out;
    bool ok = false;
    const int id = parts.at(0).toInt(&ok);
    if (!ok || parts.at(1).isEmpty() || parts.at(2).isEmpty())
        return out;
    out.datasetId = id;
    out.table = parts.at(1);
    out.column = parts.at(2);
    out.valid = true;
    return out;
}

} // namespace channel_mime

// 画布间**曲线移动**的线协议。
//
// 与通道 MIME 同一条原则：只传逻辑标识，不传 PlotSeries*/AxesView*。
// 源画布只给 id；坐标三元组让它在新画布上走同一条 plot.channel 取数路径。
// 用 \x1f 分隔（key 与 title 里可能有 '|' 与 '.'，不能用竖线）。
namespace series_mime
{

inline const char *type() { return "application/x-ngplotscope-series"; }

inline QString encode(int srcCanvasId, const QString &key, int datasetId,
                      const QString &table, const QString &column, const QString &title)
{
    const QChar sep(0x1f);
    return QString::number(srcCanvasId) + sep + QString::number(datasetId) + sep + table
           + sep + column + sep + key + sep + title;
}

struct Decoded
{
    bool valid = false;
    int srcCanvasId = -1;
    int datasetId = nextpilot::plotscope::dataset::kInvalidDataset;
    QString table;
    QString column;
    QString key;
    QString title;
};

inline Decoded decode(const QMimeData *mime)
{
    Decoded out;
    if (!mime || !mime->hasFormat(QString::fromLatin1(type())))
        return out;
    const QStringList parts =
        QString::fromUtf8(mime->data(QString::fromLatin1(type()))).split(QChar(0x1f));
    if (parts.size() != 6)
        return out;
    bool ok = false;
    const int canvasId = parts.at(0).toInt(&ok);
    if (!ok || canvasId < 0)
        return out;
    bool idOk = false;
    const int ds = parts.at(1).toInt(&idOk);
    if (!idOk || parts.at(2).isEmpty() || parts.at(3).isEmpty())
        return out;
    out.srcCanvasId = canvasId;
    out.datasetId = ds;
    out.table = parts.at(2);
    out.column = parts.at(3);
    out.key = parts.at(4);
    out.title = parts.at(5);
    out.valid = true;
    return out;
}

} // namespace series_mime
} // namespace nextpilot::plotscope::dataset
