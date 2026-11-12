/* PlotScope 内核 C ABI（app 只允许 include 这一个头文件，见 D16） */
#ifndef PS_KERNEL_H
#define PS_KERNEL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ABI 版本闸门：宿主用它挡掉不兼容的实现，见 D9 */
#define PS_ABI_VERSION 1u

/* 借用型字节片：宿主分配、内核不释放 */
typedef struct PsBytes {
    const uint8_t *ptr;
    size_t len;
} PsBytes;

uint32_t    ps_abi_version(void);   /* 恒等于 PS_ABI_VERSION */
const char *ps_kernel_version(void);/* 形如 "0.1.0"，稳定字符串，勿 free */
int32_t     ps_abi_selftest(void);  /* 0 = 通过；非 0 = 具体失败码 */

#ifdef __cplusplus
}
#endif
#endif /* PS_KERNEL_H */
