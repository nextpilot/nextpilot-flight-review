#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ps::arrow {

// 列逻辑类型；与 ps_kernel.h 的 PsValueType 码一致的子集。
enum class Type : int {
    F64 = 0,
    F32 = 1,
    I64 = 2,
    U64 = 3,
    I32 = 4,
    U32 = 5,
    Bool = 10,
    Timestamp = 20,  // 仅中间态：导入时归一到 ns 后转 I64
};

// 一列的值与 null 位图。values 按 type 解释为定长原生字节（小端），
// validity 为 (rows+7)/8 字节位图（1=null），空表示全部有效。
struct Column {
    std::string name;
    Type type = Type::F64;
    std::vector<std::byte> values;
    std::vector<uint8_t> validity;
    int time_unit = 3;  // Timestamp 列才有意义：0=s 1=ms 2=us 3=ns
};

// 一个 record batch 的展开结果；多 batch 的 IPC 流会把所有 batch 的行追加到同一组列。
struct Batch {
    std::vector<Column> columns;
    int64_t rows = 0;
};

struct ImportOptions {
    // 时间列在 Arrow schema 中的字段索引；-1 表示用行索引 0,1,2... 作 ns 时间轴。
    int timestamp_column = -1;
};

// 把 Arrow 时间单位编码值归一到纳秒。unit: 0=s 1=ms 2=us 3=ns。
// 返回 0 成功；非 0 表示单位非法或 int64 溢出（out 不变）。
int normalizeToNs(int64_t value, int unit, int64_t &out);

// 读 Arrow IPC 流（schema + 若干 record batch）。返回 0 成功，非 0 失败并写 err。
// 时间列按 options 归一到 ns 后以 I64 列返回；其余列保留原生类型。
int importIpc(const uint8_t *data, size_t size, const ImportOptions &options,
              Batch &out, std::string &err);

// 把列批写成 Arrow IPC 流（含 EOS），可被标准 Arrow 读取器消费。
// Timestamp 列按自身 time_unit 编码为 Arrow TIMESTAMP；其余按 type 定长类型。
int exportIpc(const Batch &batch, std::vector<uint8_t> &out, std::string &err);

// 把时间单位枚举名解析为 unit 码（s/ms/us/ns/usec 等），失败返回 -1。
int parseTimeUnit(const std::string &name);

}  // namespace ps::arrow
