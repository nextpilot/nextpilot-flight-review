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
    std::printf("abi=%u version=%s selftest=ok\n", ps_abi_version(), ver);
    return 0;
}
