// fastplot × ng 集成测试：真实 ULog 文件 → Rust 内核 LOD → fastplot 绘制。
//
// 这条链证明的是"库可以直接被 ng 调用"：
//   Source 字节 → IngestPipeline/ULog SPI parser → Dataset（Rust typed 表）
//     → Dataset::lodMinMax（Rust 内按像素分桶，<=px 个三元组）
//     → nextpilot::plotscope::fastplot::Curve::fromLod（C++ 只画，不重扫）→ PlotWidget
//
// 关键点：C++ 侧从头到尾没有拿到全量行，也没有复制数据。
// 找不到 fixture 时跳过（CI 上老仓库不在同一路径）。
#include "DatasetRegistry.h"
#include "IngestPipeline.h"
#include "ParserRegistry.h"
#include "ps_parser_spi.h"

#include <fastplot/PlotWidget.h>

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QString>

#include <cstdio>
#include <cstdlib>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::parser;
using namespace nextpilot::plotscope::context;

namespace
{

QString fixturePath()
{
    const QByteArray env = qgetenv("FASTPLOT_ULG_FIXTURE");
    if (!env.isEmpty())
        return QString::fromLocal8Bit(env);
    // 默认用 CMake 注入的仓库内 fixture 目录（NG_FIXTURE_DIR），不写死机器路径
    return QStringLiteral(NG_FIXTURE_DIR "/demo_flight.ulg");
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    const QString path = fixturePath();
    if (!QFile::exists(path))
    {
        fprintf(stdout, "[skip] 找不到 %s，跳过 fastplot × ng 集成测试\n",
                path.toUtf8().constData());
        return 0;
    }

    if (!ParserRegistry::entry(QStringLiteral("ulog")))
    {
        fprintf(stderr, "FAIL: ulog 解析器未注册（扩展没链进来？）\n");
        return 1;
    }

    // ---- 1) 走真实管道装载 ULog ----
    DatasetRegistry repo;
    IngestPipeline pipe(repo);
    const Result<int> bound = pipe.bindParser(1, ParserBinding{QStringLiteral("ulog"), {}, true});
    if (!bound)
    {
        fprintf(stderr, "FAIL: bindParser(ulog) 失败\n");
        return 1;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        fprintf(stderr, "FAIL: 打不开 %s\n", path.toUtf8().constData());
        return 1;
    }
    QByteArray chunk;
    while (!file.atEnd())
    {
        chunk = file.read(4096);
        if (chunk.isEmpty())
            break;
        pipe.feed(1, reinterpret_cast<const uint8_t *>(chunk.constData()), chunk.size());
    }
    file.close();
    pipe.finish(1);

    const Dataset *ds = repo.get(bound.value());
    if (!ds || ds->isNull())
    {
        fprintf(stderr, "FAIL: 管道没有产出数据集\n");
        return 1;
    }

    // ---- 2) 找 attitude.roll（找不到就退到第一个非空通道）----
    QString tableName = QStringLiteral("attitude");
    QString columnName = QStringLiteral("roll");
    const Table *table = ds->table(tableName);
    if (!table || !table->column(columnName))
    {
        tableName.clear();
        for (int i = 0; i < ds->tableCount() && tableName.isEmpty(); ++i)
        {
            const Table *t = ds->table(i);
            if (t && t->rows > 1 && !t->columns.isEmpty())
            {
                tableName = t->name;
                columnName = t->columns.first().name;
            }
        }
    }
    if (tableName.isEmpty())
    {
        fprintf(stderr, "FAIL: 数据集里没有可绘制的表\n");
        return 1;
    }
    table = ds->table(tableName);
    const Column *column = table ? table->column(columnName) : nullptr;
    if (!table || !column)
    {
        fprintf(stderr, "FAIL: 找不到 %s.%s\n", tableName.toUtf8().constData(),
                columnName.toUtf8().constData());
        return 1;
    }
    fprintf(stdout, "[ok] 装载 %s：表 %s.%s，%d 行\n", path.toUtf8().constData(),
            tableName.toUtf8().constData(), columnName.toUtf8().constData(), table->rows);

    // ---- 3) 建画布，按像素宽度向内核要 LOD ----
    nextpilot::plotscope::fastplot::PlotWidget plot;
    plot.resize(900, 420);

    const int px = static_cast<int>(plot.viewport().plotRect().width());
    // 首次布局后才有 plotRect；先 resize 再取
    const QVector<LodTriple> lod =
        ds->lodMinMax(tableName, columnName, table->firstTimeUs, table->lastTimeUs, px);
    if (lod.isEmpty())
    {
        fprintf(stderr, "FAIL: lodMinMax 返回空\n");
        return 1;
    }

    QVector<nextpilot::plotscope::fastplot::LodPoint> pts;
    pts.reserve(lod.size());
    for (const LodTriple &t : lod)
        pts.push_back(nextpilot::plotscope::fastplot::LodPoint{t.x, t.min, t.max});

    const QColor ink(0xff, 0x9f, 0x43);
    plot.addCurve(nextpilot::plotscope::fastplot::Curve::fromLod(pts, ink, 1.4));
    if (!plot.autoScale())
    {
        fprintf(stderr, "FAIL: autoScale 失败\n");
        return 1;
    }
    fprintf(stdout, "[ok] 内核 LOD：%d 行 → %d 个三联（px=%d）\n", table->rows, int(pts.size()), px);

    // ---- 4) 出图：曲线像素必须真的出现 ----
    const QImage img = plot.grabImage();
    int curvePixels = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (img.pixelColor(x, y) == ink)
                ++curvePixels;
    if (curvePixels < 300)
    {
        fprintf(stderr, "FAIL: 曲线像素过少（%d）\n", curvePixels);
        return 1;
    }

    const QString out = QDir::current().filePath(QStringLiteral("fastplot_p2_ulg.png"));
    if (!img.save(out))
    {
        fprintf(stderr, "FAIL: 写不出 %s\n", out.toUtf8().constData());
        return 1;
    }
    fprintf(stdout, "[ok] 出图：%d 个曲线像素 → %s\n", curvePixels, out.toUtf8().constData());
    return 0;
}
