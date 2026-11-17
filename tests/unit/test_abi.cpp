// 最小的 ABI 冒烟测试：不引第三方测试框架，保证任何机器都能跑。
#include <cstdio>
#include <cstring>

#include "ps_kernel.h"

namespace {

bool add(PsDataset *ds, const char *name, const double *t, const double *v, uint32_t n)
{
    char err[256] = {0};
    if (ps_dataset_add_channel(ds, name, "", t, v, n, err, sizeof(err)) != 0)
    {
        std::fprintf(stderr, "add %s failed: %s\n", name, err);
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (ps_abi_version() != PS_ABI_VERSION)
    {
        std::fprintf(stderr, "abi version mismatch: %u != %u\n", ps_abi_version(), PS_ABI_VERSION);
        return 1;
    }
    const char *ver = ps_kernel_version();
    if (ver == nullptr || std::strlen(ver) == 0)
    {
        std::fprintf(stderr, "kernel version string empty\n");
        return 2;
    }
    if (ps_abi_selftest() != 0)
    {
        std::fprintf(stderr, "selftest failed\n");
        return 3;
    }

    const double t[2] = {0.0, 1.0};
    const double roll[2] = {0.0, 2.0};
    const double one[2] = {1.0, 1.0};
    const double zero[2] = {0.0, 0.0};
    PsDataset *ds = ps_dataset_new();
    if (!ds || !add(ds, "roll", t, roll, 2) || !add(ds, "w", t, one, 2) ||
        !add(ds, "x", t, zero, 2) || !add(ds, "y", t, zero, 2) || !add(ds, "z", t, zero, 2))
    {
        ps_dataset_free(ds);
        return 4;
    }
    const uint32_t base = ps_dataset_channel_count(ds);
    if (base != 5)
    {
        std::fprintf(stderr, "base channels=%u\n", base);
        ps_dataset_free(ds);
        return 5;
    }

    char err[256] = {0};
    {
        double pt[2] = {0}, pv[2] = {0};
        uint32_t pn = 0;
        if (ps_preview_filter(ds, "roll", "moving_average", "{\"window\":2}", pt, pv, 2, &pn,
                              err, sizeof(err)) != 0 ||
            pn != 2 || pv[0] != 0.0 || pv[1] != 1.0)
        {
            std::fprintf(stderr, "preview failed: %s\n", err);
            ps_dataset_free(ds);
            return 6;
        }
        if (ps_dataset_channel_count(ds) != base)
        {
            std::fprintf(stderr, "preview mutated dataset\n");
            ps_dataset_free(ds);
            return 7;
        }
    }
    if (ps_dataset_apply_filter(ds, "roll", "moving_average", "{\"window\":2}", "roll.ma",
                                err, sizeof(err)) != 0)
    {
        std::fprintf(stderr, "filter failed: %s\n", err);
        ps_dataset_free(ds);
        return 8;
    }
    const uint32_t after = ps_dataset_channel_count(ds);
    const PsChannelInfo *ma = ps_dataset_channel(ds, after - 1);
    if (after != 6 || std::strcmp(ma->name, "roll.ma") != 0 || ma->point_count != 2 ||
        ma->value[0] != 0.0 || ma->value[1] != 1.0)
    {
        std::fprintf(stderr, "filter result mismatch\n");
        ps_dataset_free(ds);
        return 9;
    }
    if (ps_dataset_apply_quat_rpy(ds, "w", "x", "y", "z", "rpy", 1, 1, err, sizeof(err)) != 0)
    {
        std::fprintf(stderr, "quat failed: %s\n", err);
        ps_dataset_free(ds);
        return 10;
    }
    if (ps_dataset_channel_count(ds) != 9)
    {
        std::fprintf(stderr, "quat channels=%u\n", ps_dataset_channel_count(ds));
        ps_dataset_free(ds);
        return 11;
    }
    {
        const double t2[4] = {0.0, 0.5, 1.0, 1.5};
        const double v2[4] = {3.0, -2.0, 4.0, 0.0};
        const double *ts[2] = {t, t2};
        const double *vs[2] = {roll, v2};
        const uint32_t ns[2] = {2, 4};
        double lod[2 * 4 * 3] = {0};
        uint32_t got[2] = {0, 0};
        if (ps_lod_minmax_many(ts, vs, ns, 2, 0.0, 1.5, 4, lod, 12, got) != 2 ||
            got[0] == 0 || got[1] == 0)
        {
            std::fprintf(stderr, "batch lod failed: got=%u,%u\n", got[0], got[1]);
            ps_dataset_free(ds);
            return 12;
        }
    }
    ps_dataset_free(ds);

    // typed 通道也能作为滤波输入。
    {
        PsDataset *tds = ps_dataset_new();
        const int64_t tu[3] = {0, 1000000, 2000000};
        const double tv[3] = {0.0, 2.0, 4.0};
        char e[128] = {0};
        if (ps_dataset_add_table_typed(tds, "t", tu, 3, e, sizeof(e)) != 0 ||
            ps_dataset_add_table_column_typed(tds, "t", "roll", "", PS_VALUE_F64,
                                              tv, nullptr, 3, e, sizeof(e)) != 0)
        {
            std::fprintf(stderr, "typed setup failed: %s\n", e);
            ps_dataset_free(tds);
            return 13;
        }
        double pt[3] = {0}, pv[3] = {0};
        uint32_t pn = 0;
        if (ps_preview_filter(tds, "t.roll", "moving_average", "{\"window\":2}",
                              pt, pv, 3, &pn, e, sizeof(e)) != 0 || pn != 3 || pv[1] != 1.0)
        {
            std::fprintf(stderr, "typed filter failed: %s pn=%u\n", e, pn);
            ps_dataset_free(tds);
            return 14;
        }
        ps_dataset_free(tds);
    }
    std::printf("abi=%u version=%s selftest=ok transform=ok\n", ps_abi_version(), ver);
    return 0;
}
