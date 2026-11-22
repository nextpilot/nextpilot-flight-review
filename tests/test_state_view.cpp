// 状态视图验收：
//   1) FNV-1a 64 位哈希标准向量（公开算法测试向量）；
//   2) stateColorIndex golden —— 与 PJ4 pj_widgets/tests/state_colors_test.cpp 的断言值逐一相同；
//   3) Okabe-Ito 调色板 RGB 与 PJ4 kStatePalette 一致（抽查首尾 + 中段）；
//   4) Axes::setStateBands —— 行计数、渲染不崩、带子确实画进图像、清空后复原。
//
// 不引入 QtTest —— 与 test_fastplot.cpp 同款 CHECK 硬断言（Release 兼容）。
#include <fastplot/Axes.h>
#include <fastplot/StateColors.h>

#include <QApplication>
#include <QImage>
#include <QColor>

#include <cmath>
#include <cstdio>
#include <cstdlib>

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
            std::exit(1);                                                                \
        }                                                                                \
    } while (0)

using namespace nextpilot::plotscope::fastplot;

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    // ---- 1) FNV-1a 64 位标准向量 ----
    CHECK(fnv1a64("") == 0xcbf29ce484222325ULL);        // 空串 = offset basis
    CHECK(fnv1a64("a") == 0xaf63dc4c8601ec8cULL);       // 公开测试向量
    CHECK(fnv1a64("foobar") == 0x85944171f73967e8ULL);  // 公开测试向量

    // ---- 2) stateColorIndex golden（与 PJ4 state_colors_test.cpp 一致）----
    static_assert(kStatePalette.size() == 10, "palette size must match PJ4");
    static_assert(stateColorIndex("") == 7, "empty label hashes to grey");
    static_assert(stateColorIndex("True") == 7);
    static_assert(stateColorIndex("False") == 4);
    static_assert(stateColorIndex("RUNNING") == 0);
    CHECK(stateColorIndex("true") != stateColorIndex("false"));
    // 稳定性：同一标签跨调用一致（constexpr 保证，这里是防御性确认）。
    CHECK(stateColorIndex("ARMED") == stateColorIndex("ARMED"));
    // 离散分布：8 个互异标签应命中 >=3 个桶（10 桶允许碰撞，但不应全挤一桶）。
    bool seen[10] = {};
    for (const char *s : {"0", "1", "2", "3", "4", "5", "6", "7"})
        seen[stateColorIndex(s)] = true;
    int buckets = 0;
    for (bool b : seen) buckets += b;
    CHECK(buckets >= 3);

    // ---- 3) 调色板 RGB 抽查----
    {
        const QColor c = stateColor(QStringLiteral("RUNNING"));
        CHECK(c.red() == 0x00 && c.green() == 0x72 && c.blue() == 0xB2); // blue（索引 0）
        const QColor g = stateColor(QString());                          // 索引 7 = grey
        CHECK(g.red() == 0x99 && g.green() == 0x99 && g.blue() == 0x99);
        const QColor f = stateColor(QStringLiteral("False"));            // 索引 4 = sky blue
        CHECK(f.red() == 0x56 && f.green() == 0xB4 && f.blue() == 0xE9);
    }

    // ---- 4) Axes::setStateBands ----
    {
        Axes ax;
        ax.resize(640, 480);
        ax.setXRange(0.0, 10.0);
        ax.setYRange(-1.0, 1.0);
        CHECK(ax.stateRowCount() == 0);

        // 三态通道：flight_mode 0/1/2 各占一段时间。
        Axes::StateRowMark row;
        row.channel = QStringLiteral("vehicle.status.flight_mode");
        const auto seg = [](double t0, double t1, const char *label) {
            Axes::StateBandMark s;
            s.tStart = t0;
            s.tEnd = t1;
            s.label = QString::fromUtf8(label);
            return s;
        };
        row.segments << seg(0.0, 2.5, "0") << seg(2.5, 6.0, "1") << seg(6.0, 10.0, "2");
        QVector<Axes::StateRowMark> rows;
        rows << row;
        ax.setStateBands(rows);
        CHECK(ax.stateRowCount() == 1);

        // 渲染不崩且确实画了东西：段是 0.82 alpha 的调色板色叠在深背景上，
        // 匹配"合成后的期望色"（0.82*pal + 0.18*bg）而非原色。
        const QImage img = ax.grabImage();
        CHECK(!img.isNull());
        const QColor bg = ax.background();
        bool foundPalette = false;
        for (const QColor &pal : {stateColor(QStringLiteral("0")),
                                  stateColor(QStringLiteral("1")),
                                  stateColor(QStringLiteral("2"))})
        {
            const int er = qRound(0.82 * pal.red() + 0.18 * bg.red());
            const int eg = qRound(0.82 * pal.green() + 0.18 * bg.green());
            const int eb = qRound(0.82 * pal.blue() + 0.18 * bg.blue());
            for (int y = 0; y < img.height() && !foundPalette; ++y)
                for (int x = 0; x < img.width(); ++x)
                {
                    const QColor px = img.pixelColor(x, y);
                    if (qAbs(px.red() - er) <= 2 && qAbs(px.green() - eg) <= 2
                        && qAbs(px.blue() - eb) <= 2)
                    {
                        foundPalette = true;
                        break;
                    }
                }
        }
        CHECK(foundPalette); // 至少一段以调色板合成色出现

        // 两行也工作；清空复原。
        Axes::StateRowMark row2;
        row2.channel = QStringLiteral("vehicle.status.armed");
        row2.segments << seg(0.0, 5.0, "true") << seg(5.0, 10.0, "false");
        rows << row2;
        ax.setStateBands(rows);
        CHECK(ax.stateRowCount() == 2);
        ax.grabImage(); // 两行渲染不崩
        ax.clearStateBands();
        CHECK(ax.stateRowCount() == 0);
        ax.grabImage(); // 清空后渲染不崩
    }

    fprintf(stderr, "[OK] state view golden: FNV-1a vectors + PJ4 palette/index + Axes bands\n");
    return 0;
}
