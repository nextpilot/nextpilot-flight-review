// 最小的 ABI 冒烟测试：不引第三方测试框架，保证任何机器都能跑。
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ps_kernel.h"

int main() {
    if (ps_abi_version() != PS_ABI_VERSION) {
        std::fprintf(stderr, "abi version mismatch: %u != %u\n", ps_abi_version(), PS_ABI_VERSION);
        return 1;
    }
    const char* ver = ps_kernel_version();
    if (ver == nullptr || std::strlen(ver) == 0) {
        std::fprintf(stderr, "kernel version string empty\n");
        return 2;
    }
    const int32_t rc = ps_abi_selftest();
    if (rc != 0) {
        std::fprintf(stderr, "selftest failed rc=%d\n", rc);
        return 3;
    }
    // 变换闭环：CSV → MA(2) → 四元数 4→3
    const char *csv = "/tmp/ps_transform_abi.csv";
    if (FILE *f = std::fopen(csv, "w")) {
        std::fputs("t,roll,w,x,y,z\n0,0,1,0,0,0\n1,2,1,0,0,0\n", f);
        std::fclose(f);
    } else {
        std::fprintf(stderr, "cannot write %s\n", csv);
        return 4;
    }
    PsDataset *ds = nullptr;
    char err[256] = {0};
    if (ps_csv_load(csv, &ds, err, sizeof err) != 0) {
        std::fprintf(stderr, "load failed: %s\n", err);
        return 5;
    }
    const uint32_t base = ps_dataset_channel_count(ds);
    if (base != 5) {
        std::fprintf(stderr, "base channels=%u\n", base);
        return 6;
    }
    if (ps_dataset_apply_filter(ds, "roll", "moving_average", "{\"window\":2}", "roll.ma",
                                err, sizeof err) != 0) {
        std::fprintf(stderr, "filter failed: %s\n", err);
        return 7;
    }
    const uint32_t after = ps_dataset_channel_count(ds);
    const PsChannelInfo *ma = ps_dataset_channel(ds, after - 1);
    if (after != 6 || std::strcmp(ma->name, "roll.ma") != 0 || ma->point_count != 2 ||
        ma->value[0] != 0.0 || ma->value[1] != 1.0) {
        std::fprintf(stderr, "filter result mismatch\n");
        return 8;
    }
    if (ps_dataset_apply_quat_rpy(ds, "w", "x", "y", "z", "rpy", 1, 1, err, sizeof err) != 0) {
        std::fprintf(stderr, "quat failed: %s\n", err);
        return 9;
    }
    if (ps_dataset_channel_count(ds) != 9) {
        std::fprintf(stderr, "quat channels=%u\n", ps_dataset_channel_count(ds));
        return 10;
    }
    ps_dataset_free(ds);
    std::printf("abi=%u version=%s selftest=ok transform=ok\n", ps_abi_version(), ver);
    return 0;
}
