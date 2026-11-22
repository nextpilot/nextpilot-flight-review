// MarkerEngine golden 验收：。
// 时间单位改为秒（NG 全局契约；PJ4 用纳秒）。覆盖：spike 点/region+band/快捷方式/
// when 条件 DSL(above/below/between/outside/changes)/backgroundColor/event/verticalLine/
// 编译错/看门狗/输出上限/JSON 往返。
#include "MarkerEngine.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>

#include <cmath>
#include <cstdio>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::script;

namespace
{
int g_fail = 0;
#define CHECK(cond)                                                                   \
    do                                                                                \
    {                                                                                 \
        if (!(cond))                                                                  \
        {                                                                             \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
            ++g_fail;                                                                 \
        }                                                                             \
    } while (0)

MarkerSeries makeSeries(const QString &name, const QVector<double> &t, const QVector<double> &v)
{
    MarkerSeries s;
    s.name = name;
    s.time = t;
    s.value = v;
    return s;
}

MarkerSeriesProvider one(const MarkerSeries &s)
{
    MarkerSeriesProvider p;
    p.names << s.name;
    p.get = [&s](const QString &name) -> const MarkerSeries * {
        return name == s.name ? &s : nullptr;
    };
    return p;
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    MarkerEngine eng;

    // --- 1. spike：逐样本 createPointMarker，事件带值 + severity ---
    {
        auto s = makeSeries(QStringLiteral("v"), {0, 1, 2, 3, 4, 5},
                            {0.0, 0.1, 0.2, 3.0, 0.3, 0.4});
        const QByteArray rule = R"LUA(
            local s = series("v")
            for i = 1, s:size() - 1 do
              local a = s:at(i - 1)
              local b = s:at(i)
              if math.abs(b.v - a.v) > 0.8 then
                createPointMarker(b.t, b.v, {label="spike", severity="error"})
              end
            end
        )LUA";
        auto r = eng.run(rule, one(s));
        CHECK(static_cast<bool>(r));
        if (r)
        {
            CHECK(r.value().size() == 2);
            CHECK(r.value()[0].kind == MarkerKind::Event);
            CHECK(r.value()[0].hasValue);
            CHECK(std::abs(r.value()[0].tStart - 3.0) < 1e-12);
            CHECK(std::abs(r.value()[0].valueLow - 3.0) < 1e-12);
            CHECK(r.value()[0].severity == MarkerSeverity::Error);
            CHECK(r.value()[0].label == QStringLiteral("spike"));
            CHECK(std::abs(r.value()[1].tStart - 4.0) < 1e-12);
        }
    }

    // --- 2. region + value band + 快捷方式 ---
    {
        auto s = makeSeries(QStringLiteral("v"), {0, 1, 2, 3}, {0.0, 1.0, 2.0, 3.0});
        const QByteArray rule = R"LUA(
            local s = series("v")
            startMarker(s:at(1).t)
            closeMarker(s:at(3).t, {label="r", severity="warning"})
            createBandMarker(-0.5, 0.5, {label="band"})
        )LUA";
        auto r = eng.run(rule, one(s));
        CHECK(static_cast<bool>(r));
        if (r)
        {
            CHECK(r.value().size() == 2);
            CHECK(r.value()[0].kind == MarkerKind::Region);
            CHECK(std::abs(r.value()[0].tStart - 1.0) < 1e-12);
            CHECK(std::abs(r.value()[0].tEnd - 3.0) < 1e-12);
            CHECK(r.value()[0].severity == MarkerSeverity::Warning);
            CHECK(r.value()[0].label == QStringLiteral("r"));
            CHECK(r.value()[1].kind == MarkerKind::ValueBand);
            CHECK(std::abs(r.value()[1].valueLow + 0.5) < 1e-12);
            CHECK(std::abs(r.value()[1].valueHigh - 0.5) < 1e-12);
        }
    }

    // --- 3. 快捷方式：createVertical/Horizontal/Point 对齐 createMarker ---
    {
        auto s = makeSeries(QStringLiteral("v"), {0, 1}, {0.0, 1.0});
        const QByteArray rule = R"LUA(
            createVerticalMarker(1, {label="v"})
            createHorizontalMarker(0.5, {label="h"})
            createPointMarker(2, 0.25, {label="p"})
        )LUA";
        auto r = eng.run(rule, one(s));
        CHECK(static_cast<bool>(r));
        if (r)
        {
            CHECK(r.value().size() == 3);
            CHECK(r.value()[0].kind == MarkerKind::Event);
            CHECK(!r.value()[0].hasValue);
            CHECK(std::abs(r.value()[0].tStart - 1.0) < 1e-12);
            CHECK(r.value()[0].severity == MarkerSeverity::Error);
            CHECK(r.value()[0].label == QStringLiteral("v"));
            CHECK(r.value()[1].kind == MarkerKind::ValueBand);
            CHECK(std::abs(r.value()[1].valueLow - 0.5) < 1e-12);
            CHECK(std::abs(r.value()[1].valueHigh - 0.5) < 1e-12);
            CHECK(r.value()[1].severity == MarkerSeverity::Warning);
            CHECK(r.value()[2].kind == MarkerKind::Event);
            CHECK(r.value()[2].hasValue);
            CHECK(std::abs(r.value()[2].valueLow - 0.25) < 1e-12);
        }
    }

    // --- 4. when 条件 DSL：above/below/between/outside backgroundColor ---
    {
        auto s = makeSeries(QStringLiteral("v"), {0, 1, 2, 3, 4}, {0.5, 1.5, 1.8, 3.0, 0.2});
        const QByteArray rule = R"LUA(
            when("v", below(1.0)):backgroundColor("green")
            when("v", between(1.0, 2.0)):backgroundColor("yellow", {label="mid"})
            when("v", above(2.0)):backgroundColor("red")
        )LUA";
        auto r = eng.run(rule, one(s));
        CHECK(static_cast<bool>(r));
        if (r)
        {
            // below: t=0 与 t=4 各一个单点 run；between: t=1..2；above: t=3 → 共 4。
            CHECK(r.value().size() == 4);
            bool hasMid = false;
            for (const auto &m : r.value())
                if (m.label == QStringLiteral("mid")) { hasMid = true; CHECK(m.kind == MarkerKind::Region); }
            CHECK(hasMid);
            CHECK(r.value()[0].color == QStringLiteral("green"));
        }
    }

    // --- 5. when event/verticalLine：起点触发 + changes + minGap + label 函数 ---
    {
        auto s = makeSeries(QStringLiteral("mode"), {0, 1, 2, 3, 4}, {0, 0, 1, 1, 0});
        const QByteArray rule = R"LUA(
            when("mode", above(0)):event({label="up"})
            when("mode", changes()):verticalLine({label=function(v, prev) return prev .. ">" .. v end})
        )LUA";
        auto r = eng.run(rule, one(s));
        CHECK(static_cast<bool>(r));
        if (r)
        {
            // above 起点(t=2) + changes 两点(t=2,4；首样本 prev=nil 不产出) = 3
            CHECK(r.value().size() == 3);
            bool hasUp = false, hasChangeLabel = false;
            for (const auto &m : r.value())
            {
                if (m.label == QStringLiteral("up")) hasUp = true;
                if (m.label == QStringLiteral("0.0>1.0")) hasChangeLabel = true;
            }
            CHECK(hasUp);
            CHECK(hasChangeLabel);
        }
    }

    // --- 6. backgroundColor minDuration 过滤短区间 ---
    {
        auto s = makeSeries(QStringLiteral("flag"), {0, 1, 2, 3}, {1, 1, 1, 1});
        const QByteArray rule = R"LUA(
            when("flag", isTrue()):backgroundColor("red", {minDuration=2})
        )LUA";
        auto r = eng.run(rule, one(s));
        CHECK(static_cast<bool>(r));
        if (r) CHECK(r.value().size() == 1); // [0,3] 时长 3 ≥ minDuration 2，保留 1 个
    }

    // --- 7. 编译错误：空 markers + 失败 ---
    {
        auto s = makeSeries(QStringLiteral("v"), {0, 1}, {0.0, 1.0});
        auto r = eng.run("this is not valid lua (((", one(s));
        CHECK(!static_cast<bool>(r));
    }

    // --- 8. 看门狗：死循环被指令预算挡住 ---
    {
        MarkerEngine small;
        MarkerEngine::Limits lim;
        lim.instructionBudget = 50;
        small = MarkerEngine(lim);
        auto r = small.run("while true do end", MarkerSeriesProvider{});
        CHECK(!static_cast<bool>(r));
    }

    // --- 9. 输出上限 ---
    {
        MarkerEngine small;
        MarkerEngine::Limits lim;
        lim.maxMarkers = 5;
        small = MarkerEngine(lim);
        auto r = small.run("for i = 1, 100 do createMarker(i) end", MarkerSeriesProvider{});
        CHECK(!static_cast<bool>(r));
    }

    // --- 10. JSON 往返 ---
    {
        auto s = makeSeries(QStringLiteral("v"), {0, 1}, {0.0, 1.0});
        auto r = eng.run("createPointMarker(1, 2, {label=\"x\", severity=\"critical\"})", one(s));
        CHECK(static_cast<bool>(r));
        if (r)
        {
            const QJsonArray arr = markersToJson(r.value());
            CHECK(arr.size() == 1);
            const QVector<PlotMarker> back = markersFromJson(arr);
            CHECK(back.size() == 1);
            CHECK(back[0].label == QStringLiteral("x"));
            CHECK(back[0].severity == MarkerSeverity::Critical);
            CHECK(std::abs(back[0].valueLow - 2.0) < 1e-12);
        }
    }

    if (g_fail == 0)
        std::printf("test_marker_engine: ALL PASS\n");
    else
        std::fprintf(stderr, "test_marker_engine: %d FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
