#pragma once

#include "protocol/ProtocolDocument.h"
#include "protocol/TypedVec.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nextpilot::plotscope::protocol
{

// psproto 的编译层（S2 后半）。
//
// 目的：模板只在启动时解析/校验一次，之后每帧解码走「段表 + 表达式字节码」，
// 不再逐帧递归遍历 Expr 的 shared_ptr 树，也不再按名字查 map。
//
// 分工：
//   - ProtocolDocument 是**人可编辑**的类型化模型（编辑器、校验器、序列化共用）；
//   - CompiledProgram 是**解码用**的紧凑产物，由 compileProgram() 生成，
//     生命周期由调用方持有（一次编译、多帧复用）；
//   - 表达式树被压成后缀字节码（栈式），槽位索引在编译期解析好。

struct Instr
{
    enum Op : std::uint8_t
    {
        Const = 0, // 压入 imm
        Load,      // 压入 slots[slot] 的唯一元素（数组则报错）
        LoadIdx,   // 压入 slots[slot][弹栈结果]
        Neg,
        Add, Sub, Mul, Div, Mod,
        BitAnd, BitOr, BitXor, Shl, Shr,
        Lt, Le, Gt, Ge, Eq, Ne,
        ToF64, ToI64, ToU64,
        Abs,
        Min, Max, Clamp
    };
    Op op = Const;
    TypedValue imm;   // Const 用
    int32_t slot = -1; // Load/LoadIdx 用
};

// 一段后缀字节码。全常量表达式在编译期直接折叠成 constValue/constant。
struct ByteCode
{
    std::vector<Instr> code;
    bool constant = false;
    TypedValue constValue;
};

// 槽位：一个段的运行期列。顶层段与各变体段共用一个扁平命名空间，
// 这样表达式里的名字在编译期就能解析成整数下标。
struct CompiledSlot
{
    std::string name;
    ValueType type = ValueType::U8;
    uint32_t elemSize = 1;
    bool bigEndian = false;
};

struct CompiledCheck
{
    std::string op;
    ByteCode rhs;
    ByteCode rhs2;
    std::vector<TypedValue> values;
    // over 在编译期解析成槽位集合（按顶层段顺序，payload 用 -1 表示伪段区间）
    std::vector<int32_t> overSlots;
    // 序列校验（crc/sum/xor/parity）
    std::uint32_t poly = 0, init = 0, xorout = 0;
    bool refin = false, refout = false;
    int width = 0;
    bool isSequence = false;
    bool isMask = false;
};

struct CompiledLength
{
    bool enabled = false;
    std::vector<int32_t> fields; // 槽位下标；payload 用 -1
    bool bytesUnit = true;
    ByteCode expr;
};

struct CompiledSeg
{
    std::string name;
    std::string unit;
    std::string prefix;
    int32_t slot = -1;
    ValueType type = ValueType::U8;
    uint32_t elemSize = 1;
    bool bigEndian = false;
    // count 为常量时直接给出元素数与字节数，省掉每帧求值。
    bool countIsConst = true;
    uint32_t constCount = 0;
    uint32_t constBytes = 0;
    ByteCode count;
    // 常量匹配：编译期折叠成常量时只比较一次。
    bool hasExpect = false;
    bool expectIsConst = false;
    TypedValue expectConst;
    ByteCode expect;
    std::vector<CompiledCheck> checks;
    CompiledLength length;
    // 物理换算：恒等时不产生新列。
    bool identityPhys = true;
    double scale = 1.0;
    double offset = 0.0;
    // 输出形态（编译期定死，热路径只做一次 int 判断）
    int emitMode = 0; // 0=channel 1=aux 2=none

    // 位域（SPEC §6）：段不推进字节游标，只对容器字节区间取一个位窗口。
    bool hasBit = false;
    int32_t bitBaseSlot = -1; // -1 = 相对帧起点
    int64_t bitOffset = 0;    // 位偏移
    uint8_t bitWidth = 0;
    bool bitMsb0 = false;     // true = Motorola/msb0，false = Intel/lsb0
    bool bitSigned = false;   // 按 type 的符号性做符号扩展

    // 段级条件：不成立时该段不存在（不占字节、不出通道、槽位为空）。
    bool hasCond = false;
    ByteCode cond;

    bool isText = false;
};

struct CompiledVariant
{
    std::string name;
    std::vector<std::string> keys; // 已归一成 "n:1" / "s:GPGGA"
    std::vector<int32_t> segs;     // 本变体的段在 segs 表中的下标
};

struct CompiledProgram
{
    std::vector<CompiledSlot> slotTable;
    std::vector<CompiledSeg> segs;      // 顶层段
    std::vector<CompiledSeg> varSegs;   // 所有变体段（扁平存放）
    std::vector<CompiledVariant> variants;
    int32_t payloadSlot = -1;
    int32_t selectorSlot = -1;
    bool hasSeries = false;
    bool fallbackReject = true;

    bool hasTimestamp = false;
    ByteCode timestamp;
    std::string timestampUnit;
    std::string timestampOrigin;
    bool timestampRaw = false;
    bool timestampIndexFallback = false;

    uint32_t maxsize = 4096;
};

// 编译：一次性把名字解析、常量折叠、字节码生成全部做完。
// 失败时 err 给出行级原因，调用方据此拒绝这份模板。
bool compileProgram(const ProtocolDocument &doc, CompiledProgram &out, std::string &err);

// 执行一段字节码。slots 按 CompiledSlot 下标排列，未填充的槽视为错误。
bool runByteCode(const ByteCode &bc, const std::vector<TypedVec> &cols, TypedValue &out,
                 std::string &err);

} // namespace nextpilot::plotscope::protocol
