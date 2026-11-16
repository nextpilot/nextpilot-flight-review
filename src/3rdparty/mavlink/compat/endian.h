// MSVC 没有 POSIX 的 <endian.h>，这里补一个最小实现。
// Windows 目标都是小端，host 转 little-endian 即恒等；大端方向用 _byteswap 实现。
// msgmap.hpp 还直接用了 ssize_t，MSVC 也没有，一并补上。
#pragma once

#include <stdint.h>

#if defined(_MSC_VER)
#include <BaseTsd.h>
#include <intrin.h>

#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef SSIZE_T ssize_t;
#endif

static __inline uint16_t htobe16(uint16_t v) { return _byteswap_ushort(v); }
static __inline uint32_t htobe32(uint32_t v) { return _byteswap_ulong(v); }
static __inline uint64_t htobe64(uint64_t v) { return _byteswap_uint64(v); }

static __inline uint16_t htole16(uint16_t v) { return v; }
static __inline uint32_t htole32(uint32_t v) { return v; }
static __inline uint64_t htole64(uint64_t v) { return v; }

static __inline uint16_t be16toh(uint16_t v) { return _byteswap_ushort(v); }
static __inline uint32_t be32toh(uint32_t v) { return _byteswap_ulong(v); }
static __inline uint64_t be64toh(uint64_t v) { return _byteswap_uint64(v); }

static __inline uint16_t le16toh(uint16_t v) { return v; }
static __inline uint32_t le32toh(uint32_t v) { return v; }
static __inline uint64_t le64toh(uint64_t v) { return v; }
#endif
