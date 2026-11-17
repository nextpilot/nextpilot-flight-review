// S1 性能验收：1000 万点切片的内存与耗时，以及 50 源枚举。
// 指标来自方案 §8 S1：slice 内存 < 1 MB、< 50 ms；50 源枚举 < 3 s。
#include "datasource/HostDataBridge.h"
#include "ps_kernel.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <vector>

using Clock = std::chrono::steady_clock;

static double msSince(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main()
{
    const uint32_t n = 10000000u;
    std::vector<double> t(n);
    std::vector<double> v(n);
    for (uint32_t i = 0; i < n; ++i)
    {
        t[i] = i * 0.001;
        v[i] = std::sin(t[i]);
    }

    PsDataset *ds = ps_dataset_new();
    assert(ds);
    char err[256] = {0};
    assert(ps_dataset_add_channel(ds, "x", "", t.data(), v.data(), n, err, sizeof(err)) == 0);

    std::deque<ps::data_source::DataSource> store;
    ps::data_source::DataSource src;
    src.config.id = 0;
    src.config.name = QStringLiteral("big");
    src.ds = ds;
    src.channels.append(ps_dataset_channel(ds, 0));
    store.push_back(src);

    ps::data_source::HostDataBridge bridge(store);

    QVector<double> ot;
    QVector<double> ov;
    const auto t0 = Clock::now();
    const ps::host::Result r = bridge.slice(0, QStringLiteral("x"), 0.0, t[n - 1], 4096, &ot, &ov);
    const double elapsed = msSince(t0);

    assert(r.ok);
    assert(ot.size() == ov.size());
    assert(ot.size() <= 4096u);
    const double bytes = static_cast<double>(ot.size() + ov.size()) * sizeof(double);

    std::printf("slice: %d pts, %.1f KB, %.2f ms\n", static_cast<int>(ot.size()), bytes / 1024.0,
                elapsed);
    if (bytes > 1024.0 * 1024.0)
    {
        std::cerr << "slice 输出超过 1 MB\n";
        return 1;
    }
    if (elapsed > 50.0)
    {
        std::cerr << "slice 耗时超过 50 ms\n";
        return 1;
    }

    // 通道统计：vMin/vMax 要全扫，1000 万点的代价在这里体现
    const auto tc = Clock::now();
    const QVector<ps::host::ChannelInfo> chans = bridge.channels(0);
    std::printf("channels(10M): %d 通道, %.2f ms\n", static_cast<int>(chans.size()),
                msSince(tc));
    assert(chans.size() == 1);
    assert(chans[0].vMin <= -0.99 && chans[0].vMax >= 0.99);

    // 50 源枚举：只查 sources()，不触通道统计
    std::deque<ps::data_source::DataSource> many;
    for (int i = 0; i < 50; ++i)
    {
        ps::data_source::DataSource s;
        s.config.id = i;
        s.config.name = QStringLiteral("src%1").arg(i);
        many.push_back(s);
    }
    ps::data_source::HostDataBridge manyBridge(many);
    const auto t1 = Clock::now();
    const QVector<ps::host::SourceInfo> list = manyBridge.sources();
    const double enumMs = msSince(t1);
    std::printf("sources(): %d 源, %.2f ms\n", static_cast<int>(list.size()), enumMs);
    assert(list.size() == 50);
    if (enumMs > 3000.0)
    {
        std::cerr << "50 源枚举超过 3 s\n";
        return 1;
    }

    ps_dataset_free(ds);
    std::printf("script slice perf ok\n");
    return 0;
}
