// 内置过滤器库 golden 验收：。
// 12 个 bundled 过滤器从内嵌 Luau 源加载，逐个断言行为（抑制/显式时间/时间重基/PJ3 pad-with-current）。
#include "FilterCatalogue.h"

#include <QCoreApplication>
#include <QJsonObject>

#include <cmath>
#include <cstdio>
#include <memory>

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

// 驱动一段样本，返回每个样本的结果（suppress 也保留占位）。
QVector<FilterSampleResult> drive(FilterInstance &f, const QVector<double> &ts, const QVector<double> &vs)
{
    QVector<FilterSampleResult> out;
    for (int i = 0; i < ts.size(); ++i)
        out.push_back(f.calculate(ts.value(i), vs.value(i)));
    return out;
}

QJsonObject P(std::initializer_list<QPair<QString, QJsonValue>> kv)
{
    QJsonObject o;
    for (const auto &p : kv)
        o.insert(p.first, p.second);
    return o;
}

// Result<unique_ptr> 不能拷贝：先落局部再 move 出来。
std::unique_ptr<FilterInstance> take(Result<std::unique_ptr<FilterInstance>> r)
{
    CHECK(static_cast<bool>(r));
    return std::move(r.value());
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    auto cat = makeBundledFilterCatalogue();
    // --- 1. 12 个内置类全部注册 ---
    CHECK(cat->entries().size() == 12);
    const char *ids[] = {"none", "absolute", "scale", "derivative", "integral", "moving_average",
                         "moving_rms", "moving_variance", "outlier_removal", "samples_counter",
                         "binary_filter", "time_since_previous"};
    for (const char *id : ids)
        CHECK(cat->find(QString::fromUtf8(id)) != nullptr);
    // descriptor 字段可读。
    const FilterDescriptor *scaleDesc = cat->find(QStringLiteral("scale"))->cls.id == QStringLiteral("scale")
                                            ? &cat->find(QStringLiteral("scale"))->cls
                                            : nullptr;
    CHECK(scaleDesc != nullptr && scaleDesc->parameters.size() == 3);
    // Scale 源里未声明 output → 默认 double；None 声明了 output="same"。
    CHECK(scaleDesc->outputKind == QStringLiteral("double"));
    CHECK(cat->find(QStringLiteral("none"))->cls.outputKind == QStringLiteral("same"));
    CHECK(cat->find(QStringLiteral("binary_filter"))->cls.outputKind == QStringLiteral("int64"));

    // --- 2. none：透传 ---
    {
        auto f = take(cat->makeProcessor(QStringLiteral("none"), QJsonObject()));
        auto r = f->calculate(1.0, 3.0);
        CHECK(!r.suppress);
        CHECK(std::abs(r.value - 3.0) < 1e-12);
    }

    // --- 3. absolute：|v| ---
    {
        auto f = take(cat->makeProcessor(QStringLiteral("absolute"), QJsonObject()));
        CHECK(std::abs(f->calculate(0.0, -2.5).value - 2.5) < 1e-12);
    }

    // --- 4. scale：仿射 + 时间偏移（显式时间） ---
    {
        auto f = take(cat->makeProcessor(QStringLiteral("scale"),
                                    P({{QStringLiteral("value_scale"), 2.0},
                                       {QStringLiteral("value_offset"), -1.0},
                                       {QStringLiteral("time_offset_sec"), 0.25}}))
                     );
        auto r = f->calculate(1.0, 10.0);
        CHECK(!r.suppress);
        CHECK(r.hasExplicitTime);
        CHECK(std::abs(r.outTime - 1.25) < 1e-12);
        CHECK(std::abs(r.value - 19.0) < 1e-12);
    }
    // 默认参数 = 恒等。
    {
        auto f = take(cat->makeProcessor(QStringLiteral("scale"), QJsonObject()));
        auto r = f->calculate(7.0, 7.5);
        CHECK(std::abs(r.value - 7.5) < 1e-12);
        CHECK(std::abs(r.outTime - 7.0) < 1e-12);
    }

    // --- 5. derivative：首样本抑制 / 斜率 / custom_dt / reset ---
    {
        auto f = take(cat->makeProcessor(QStringLiteral("derivative"), QJsonObject()));
        auto out = drive(*f, {0, 1, 2, 3}, {0, 3, 6, 9});
        CHECK(out[0].suppress);
        CHECK(!out[1].suppress && std::abs(out[1].value - 3.0) < 1e-12);
        CHECK(std::abs(out[2].value - 3.0) < 1e-12);
        // 亚秒斜率。
        auto f2 = take(cat->makeProcessor(QStringLiteral("derivative"), QJsonObject()));
        f2->calculate(0.0, 10.0);
        CHECK(std::abs(f2->calculate(0.5, 11.0).value - 2.0) < 1e-12);
        // dt==0 → 0，不是 inf/nan。
        auto f3 = take(cat->makeProcessor(QStringLiteral("derivative"), QJsonObject()));
        f3->calculate(1.0, 5.0);
        CHECK(std::abs(f3->calculate(1.0, 9.0).value) < 1e-12);
        // custom_dt 覆盖实测间隔。
        auto f4 = take(cat->makeProcessor(QStringLiteral("derivative"),
                                     P({{QStringLiteral("use_custom_dt"), true},
                                        {QStringLiteral("custom_dt"), 2.0}}))
                      );
        f4->calculate(0.0, 0.0);
        CHECK(std::abs(f4->calculate(1.0, 8.0).value - 4.0) < 1e-12);
        // reset 后首样本重新抑制。
        f->reset();
        CHECK(f->calculate(4.0, 100.0).suppress);
    }

    // --- 6. integral：梯形面积 / 首样本抑制 / reset ---
    {
        auto f = take(cat->makeProcessor(QStringLiteral("integral"), QJsonObject()));
        auto out = drive(*f, {0, 1, 2, 3, 4, 5}, {1, 1, 1, 1, 1, 1});
        CHECK(out[0].suppress);
        CHECK(std::abs(out[1].value - 1.0) < 1e-12);
        CHECK(std::abs(out[5].value - 5.0) < 1e-12);
        // 斜坡 0,2,4 → 累计 1, 4。
        auto f2 = take(cat->makeProcessor(QStringLiteral("integral"), QJsonObject()));
        f2->calculate(0.0, 0.0);
        CHECK(std::abs(f2->calculate(1.0, 2.0).value - 1.0) < 1e-12);
        CHECK(std::abs(f2->calculate(2.0, 4.0).value - 4.0) < 1e-12);
        // custom_dt 忽略时间戳。
        auto f3 = take(cat->makeProcessor(QStringLiteral("integral"),
                                     P({{QStringLiteral("use_custom_dt"), true},
                                        {QStringLiteral("custom_dt"), 2.0}}))
                      );
        f3->calculate(0.0, 1.0);
        CHECK(std::abs(f3->calculate(5.0, 1.0).value - 2.0) < 1e-12);
        CHECK(std::abs(f3->calculate(7.0, 1.0).value - 4.0) < 1e-12);
    }

    // --- 7. moving_average：PJ3 pad-with-current ---
    {
        auto f = take(cat->makeProcessor(QStringLiteral("moving_average"),
                                    P({{QStringLiteral("window"), 3}}))
                     );
        CHECK(std::abs(f->calculate(0.0, 10.0).value - 10.0) < 1e-12);
        CHECK(std::abs(f->calculate(1.0, 20.0).value - 50.0 / 3.0) < 1e-12);
        CHECK(std::abs(f->calculate(2.0, 30.0).value - 20.0) < 1e-12);
        CHECK(std::abs(f->calculate(3.0, 0.0).value - 50.0 / 3.0) < 1e-12);
        // 补偿时间：居中到缓冲跨度中点。
        auto f2 = take(cat->makeProcessor(QStringLiteral("moving_average"),
                                     P({{QStringLiteral("window"), 10},
                                        {QStringLiteral("compensate_time_offset"), true}}))
                      );
        f2->calculate(0.0, 1.0);
        auto mid = f2->calculate(1.0, 2.0);
        CHECK(mid.hasExplicitTime);
        CHECK(std::abs(mid.outTime - 0.5) < 1e-12);
    }

    // --- 8. moving_rms：pad-with-current + 手算 window=2 ---
    {
        auto f = take(cat->makeProcessor(QStringLiteral("moving_rms"),
                                    P({{QStringLiteral("window"), 2}}))
                     );
        CHECK(std::abs(f->calculate(0.0, 3.0).value - 3.0) < 1e-12);
        CHECK(std::abs(f->calculate(1.0, 4.0).value - std::sqrt(25.0 / 2.0)) < 1e-12);
        CHECK(std::abs(f->calculate(2.0, 5.0).value - std::sqrt((16.0 + 25.0) / 2.0)) < 1e-12);
    }

    // --- 9. moving_variance：方差 / stdev ---
    {
        auto f = take(cat->makeProcessor(QStringLiteral("moving_variance"),
                                    P({{QStringLiteral("window"), 10}}))
                     );
        FilterSampleResult last;
        for (int y = 1; y <= 10; ++y)
            last = f->calculate(y, static_cast<double>(y));
        CHECK(std::abs(last.value - 8.25) < 1e-9);
        auto f3 = take(cat->makeProcessor(QStringLiteral("moving_variance"),
                                     P({{QStringLiteral("window"), 10},
                                        {QStringLiteral("std_dev"), true}}))
                      );
        FilterSampleResult stdLast;
        for (int y = 1; y <= 10; ++y)
            stdLast = f3->calculate(y, static_cast<double>(y));
        CHECK(std::abs(stdLast.value - std::sqrt(8.25)) < 1e-9);
    }

    // --- 10. outlier_removal：丢尖峰 + 一样本延迟（显式时间） ---
    {
        auto f = take(cat->makeProcessor(QStringLiteral("outlier_removal"),
                                    P({{QStringLiteral("outlier_factor"), 5.0}}))
                     );
        const QVector<double> ys = {0, 1, 2, 100, 4, 5, 6};
        auto out = drive(*f, {0, 1, 2, 3, 4, 5, 6}, ys);
        CHECK(!out[0].suppress && std::abs(out[0].value - 0.0) < 1e-12);
        CHECK(!out[3].suppress && std::abs(out[3].value - 2.0) < 1e-12); // i=3 发上一拍 2
        CHECK(out[4].suppress);                                         // i=4 尖峰被抑制
        CHECK(!out[5].suppress && std::abs(out[5].value - 4.0) < 1e-12);
        CHECK(!out[6].suppress && std::abs(out[6].value - 5.0) < 1e-12);
    }

    // --- 11. samples_counter：滑窗计数 ---
    {
        auto f = take(cat->makeProcessor(QStringLiteral("samples_counter"),
                                    P({{QStringLiteral("samples_ms"), 3.0}}))
                     );
        // t 以秒计；窗口 3ms。样本间隔 1ms 上升至 4 后平台。
        CHECK(f->calculate(0.000, 0).value == 1);
        CHECK(f->calculate(0.001, 0).value == 2);
        CHECK(f->calculate(0.002, 0).value == 3);
        CHECK(f->calculate(0.003, 0).value == 4);
        CHECK(f->calculate(0.004, 0).value == 4);
        CHECK(f->calculate(0.005, 0).value == 4);
    }

    // --- 12. binary_filter：6 种比较 ---
    {
        auto greater = take(cat->makeProcessor(QStringLiteral("binary_filter"),
                                          P({{QStringLiteral("binary_op"), 3},
                                             {QStringLiteral("binary_a"), 5.0}}))
                           );
        CHECK(greater->calculate(0, 6.0).value == 1);
        CHECK(greater->calculate(1, 5.0).value == 0);
        CHECK(greater->calculate(2, 4.0).value == 0);
        auto range = take(cat->makeProcessor(QStringLiteral("binary_filter"),
                                        P({{QStringLiteral("binary_op"), 5},
                                           {QStringLiteral("binary_a"), 2.0},
                                           {QStringLiteral("binary_b"), 8.0}}))
                         );
        CHECK(range->calculate(0, 2.0).value == 1);
        CHECK(range->calculate(1, 5.0).value == 1);
        CHECK(range->calculate(2, 8.0).value == 1);
        CHECK(range->calculate(3, 8.1).value == 0);
        // 反区间恒 0（不自动交换）。
        auto inv = take(cat->makeProcessor(QStringLiteral("binary_filter"),
                                      P({{QStringLiteral("binary_op"), 5},
                                         {QStringLiteral("binary_a"), 8.0},
                                         {QStringLiteral("binary_b"), 2.0}}))
                       );
        CHECK(inv->calculate(0, 5.0).value == 0);
    }

    // --- 13. time_since_previous：首样本抑制 + 秒差 ---
    {
        auto f = take(cat->makeProcessor(QStringLiteral("time_since_previous"), QJsonObject()));
        CHECK(f->calculate(0.0, 0).suppress);
        CHECK(std::abs(f->calculate(1.0, 0).value - 1.0) < 1e-12);
        CHECK(std::abs(f->calculate(3.0, 0).value - 2.0) < 1e-12);
    }

    // --- 14. standaloneClassScript：自包含模块 + create 注入默认值 ---
    {
        auto script = cat->standaloneClassScript(QStringLiteral("scale"));
        CHECK(script.has_value());
        CHECK(!cat->standaloneClassScript(QStringLiteral("bad id!")).has_value()); // 注入防护
        if (script)
        {
            // 用该脚本单独建一个引擎实例：create(given) 只给 value_scale，其余走 default。
            auto solo = makeLuaFilterEngine();
            auto classes = solo->inspectModule(*script, QStringLiteral("solo"));
            CHECK(static_cast<bool>(classes) && classes.value().size() == 1);
            auto f = take(solo->createInstance(classes.value()[0], P({{QStringLiteral("value_scale"), 3.0}})));
            auto r = f->calculate(2.0, 4.0);
            // value_scale=3, value_offset=0(default), time_offset_sec=0(default) → 12, t 不变
            CHECK(std::abs(r.value - 12.0) < 1e-9);
            CHECK(std::abs(r.outTime - 2.0) < 1e-9);
        }
    }

    // --- 15. makeProcessorFromSource：布局内嵌源恢复路径 ---
    {
        auto r = cat->makeProcessorFromSource(bundledBuiltinFiltersSource(), QStringLiteral("absolute"), QJsonObject());
        CHECK(static_cast<bool>(r));
    }

    if (g_fail == 0)
        std::printf("test_filter_catalogue: ALL PASS\n");
    else
        std::fprintf(stderr, "test_filter_catalogue: %d FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
