#pragma once

#include <QStringList>

#include <cstddef>
#include <cstdint>
#include <string>

namespace nextpilot::plotscope::protocol
{

// 摘要算法（SPEC §10）：内置算子表 + 可选脚本后端。
//
// 两条硬约束：
//   1. 输入永远是记录的**连续原始字节**，不是字段值拼回去——分隔符被吃掉后拼回必错；
//   2. 算子名是唯一来源：validate、编译、文档、编辑器下拉都从 digestAlgorithmNames() 取，
//      不允许各处各写一份 switch。

/// 内置算子名（含规范名，不含别名）。
QStringList digestAlgorithmNames();

/// 别名归一：nmea_xor → xor8（NMEA 就是"$ 之后到 * 之前"这段字节的 XOR8）。
std::string normalizeDigestAlg(const std::string &alg);

/// 内置算子名是否合法（先归一别名）。
bool digestAlgKnown(const std::string &alg);

/// 计算内置算子。失败写 err（未知算子 / 数据长度与算法不匹配）。
bool computeDigest(const std::string &alg, const uint8_t *data, size_t len, uint64_t &out,
                   std::string &err);

/// 脚本后端是否可用（编译期可选，未启用时含 code 的模板在加载期就报 CONFIG_INVALID）。
bool scriptDigestAvailable();

/// 计算 Lua 脚本：ctx.data（字节数组，1 基）、ctx.len、ctx.fields（段值）。
/// fields 以 "名字" → TypedVec 传入，脚本按需取；返回整数或布尔。
struct ScriptDigestInput
{
    const uint8_t *data = nullptr;
    size_t len = 0;
    // 段名 → 值列（每段一列，数组段是整列）。字符串值以 UTF-8 传入。
    void *fields = nullptr; // 内部用 std::map<std::string, TypedVec>*，避免头文件拖入 TypedVec
};

bool computeScriptDigest(const std::string &code, const ScriptDigestInput &in, uint64_t &out,
                         std::string &err, bool *timeout = nullptr);

} // namespace nextpilot::plotscope::protocol
