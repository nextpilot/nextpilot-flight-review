#include "filter/FilterCatalog.h"

#include "ps_kernel.h"

#include <cmath>
#include <cstdio>
#include <cstring>

int main()
{
    std::size_t count = 0;
    const ps::filter::Def *defs = ps::filter::catalog(&count);
    if (!defs || count < 13)
    {
        std::fprintf(stderr, "filter catalog too small: %zu\n", count);
        return 1;
    }

    const double t[] = {0.0, 1.0, 2.0, 3.0};
    const double v[] = {-1.0, 0.0, 3.0, 4.0};
    const uint32_t n = 4;
    double ot[n] = {0};
    double ov[n] = {0};

    for (std::size_t i = 0; i < count; ++i)
    {
        uint32_t got = 0;
        char err[256] = {0};
        const int rc = ps_filter_apply_series(defs[i].id, "{}", t, v, n,
                                              ot, ov, n, &got, err, sizeof err);
        if (rc != 0)
        {
            std::fprintf(stderr, "filter %s failed: %s\n", defs[i].id, err);
            return 1;
        }
        if (got == 0 || got > n)
        {
            std::fprintf(stderr, "filter %s returned %u points\n", defs[i].id, got);
            return 1;
        }
    }

    // 确认 catalog 与内核实现没有漂移：未知 id 必须报错。
    uint32_t got = 0;
    char err[256] = {0};
    if (ps_filter_apply_series("not_a_filter", "{}", t, v, n, ot, ov, n,
                               &got, err, sizeof err) == 0)
    {
        std::fprintf(stderr, "unknown filter unexpectedly succeeded\n");
        return 1;
    }

    std::puts("filter catalog ok");
    return 0;
}
