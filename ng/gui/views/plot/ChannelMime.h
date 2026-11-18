#pragma once

// 通道拖拽的线协议：数据树与画布之间唯一的"约定"。
//
// 为什么不传裸指针（老代码传 PsChannelInfo*）：
//   · 指针跨视图传递，生命周期无人担保，视图析构后就是悬垂；
//   · 指针无法序列化，脚本/回放/跨进程一律失效；
//   · 它把"绘图视图"和"数据源内部结构"焊死，违反星形解耦。
// 传稳定的**逻辑标识**三元组（datasetId + table + column）——
// 画布拿到后发 plot.channel 意图取数，与双击走完全相同的路径。
//
// 视图之间仍互不 include：数据树只 encode，画布只 decode，
// 两者唯一共享的是这张无依赖的小头文件。

#include "Dataset.h"

#include <QMimeData>
#include <QString>

namespace ng::gui
{
namespace channel_mime
{

// 自定义 MIME 类型（Qt 约定：application/x-<vendor>-<type>）。
inline const char *type() { return "application/x-ngplotscope-channel"; }

inline QString encode(ng::core::DatasetId datasetId, const QString &table, const QString &column)
{
    // 格式： "datasetId|table|column"，表名/列名取自 schema，不含 '|'（parser 已清洗）。
    return QString::number(datasetId) + QLatin1Char('|') + table + QLatin1Char('|') + column;
}

struct Decoded
{
    bool valid = false;
    ng::core::DatasetId datasetId = ng::core::kInvalidDataset;
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
} // namespace ng::gui
