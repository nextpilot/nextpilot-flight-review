#pragma once

// 最小 xlsx 写手：单簿多 sheet，数值/文本混排。
//
// 为什么不引第三方库：ng 的依赖清单里没有任何 zip/xlsx 库，而"多表快照导出"
// 只需要"写 xlsx 的一半"——不需要样式、图表、公式。为半张表引一个库，
// 会把 gui 的发布依赖抬上去；这里用 store-only zip（不压缩）+ 手写 OOXML，
// 约 250 行、零依赖、可回归（测试用 python zipfile 直接读回校验）。
//
// 明确不做的：压缩（store 模式，体积大但合法）、样式、公式、合并单元格。
// 需要这些时再引库，不在这里长。

#include <QString>
#include <QStringList>
#include <QVector>

namespace nextpilot::plotscope::host
{

class XlsxBook
{
public:
    struct Cell
    {
        QString text;          // isNumber=false 时使用（inlineStr）
        double number = 0.0;   // isNumber=true 时使用
        bool isNumber = false;

        static Cell ofNumber(double v) { Cell c; c.number = v; c.isNumber = true; return c; }
        static Cell ofText(const QString &t) { Cell c; c.text = t; return c; }
    };

    struct Sheet
    {
        QString name;
        QStringList headers;
        QVector<QVector<Cell>> rows; // 每行列数与 headers 等长（缺的留空）
    };

    // 追加一个 sheet，返回引用（调用方直接塞 headers/rows）。
    // 名字会被清洗（去掉 []:*?/\、截到 31 字符、重名加 _2）——xlsx 的硬约束。
    Sheet &addSheet(const QString &name);
    int sheetCount() const { return m_sheets.size(); }

    // 写盘。失败返回 false 并带原因（目录不存在/只读/磁盘满）。
    bool save(const QString &path, QString *err = nullptr);

private:
    QVector<Sheet> m_sheets;
};

} // namespace nextpilot::plotscope::host
