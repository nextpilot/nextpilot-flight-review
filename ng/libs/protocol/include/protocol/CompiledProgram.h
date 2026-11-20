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
    // 非线性 transform：表达式逐元素求值（self 编译时已重写成该段自己的槽位）。
    bool hasTransformExpr = false;
    ByteCode transformCode;

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

    // ---- text 词法（SPEC §7）：段边界来自字面量/定界符，不是字节宽度 ----
    bool isTextSeg = false;
    int boundary = 0;                    // SegBoundary：0 Byte / 1 Literal / 2 Delimit / 3 Fixed / 4 Rest / 5 Repeat
    bool isLiteral = false; // 字面量段：精确匹配、不产通道（如 $ 或 *）
    std::string key;        // key=value：非空时按 "key=" 定位
    std::string literal;
    bool hasDelimiter = false;
    bool blankDelim = false;             // delimiter:"blank" → 空格/Tab 字符类
    std::vector<std::string> delimiters; // 候选集合（已归一，最长匹配优先）
    bool repeatAll = false;              // repeater:"*" 重复到下一段边界/记录尾
    bool isRest = false;                 // 整块原文，不切分
    bool hasWidth = false;               // fixed 词法：定宽字符数
    uint32_t width = 0;
    int textType = 0;                   // 0=number 1=text（string/char）
    int64_t base = 10;
    bool hasFixedDigits = false;
    uint32_t fixedDigits = 0;
    bool signedFlag = false;
    bool sci = false;
    std::string emptyPolicy;             // "" | nan | skip | raw
    std::string pattern;
    int onMismatch = 0;                  // 0=reject 1=warn 2=skip
    std::vector<TransformStep> transform;
    // digest 端点：编译期解析成段下标。 -1 = frame_start，-2 = frame_end，-3 = before:self
    int32_t digestFrom = -1;
    int32_t digestTo = -3;
    int digestKind = 0;                  // 0=none 1=builtin 2=script
    std::string digestAlg;
    std::string digestScript;
    bool hasDerive = false;              // 派生字段：不读数据，由表达式算出
    ByteCode derive;
    bool isTime = false;                 // type:"time"：text 文本时间 → 当天秒
    int repeatMode = 0;                  // 0 none / 1 eos / 2 until
    ByteCode repeatUntil;
    bool digestHasInject = false;        // digest.inject：数据尾部追加的常量字节（CRC_EXTRA）
    uint32_t digestInject = 0;
    bool hasRange = false;               // 值域/枚举校验
    double rangeMin = 0.0;
    double rangeMax = 0.0;
    bool hasEnum = false;
    std::vector<double> enumValues;
    bool hasDigestBounds = false;        // 有 digest 段（校验输出的那个）
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

    // ---- 记录层（text）：词法模式 + 帧尾候选集合 ----
    bool isText = false;
    bool isDelimited = false; // 存在 delimiter 段（记录按切分解析）
    bool hasTerminator = false;
    std::vector<std::string> terminator;
    // 用于流式定位的帧首字面量（第一个 literal 段的值）；空表示靠 terminator 分帧。
    bool hasStartAnchor = false;
    std::string startAnchor;
};

// 编译：一次性把名字解析、常量折叠、字节码生成全部做完。
// 失败时 err 给出行级原因，调用方据此拒绝这份模板。
bool compileProgram(const ProtocolDocument &doc, CompiledProgram &out, std::string &err);

// 执行一段字节码。slots 按 CompiledSlot 下标排列，未填充的槽视为错误。
bool runByteCode(const ByteCode &bc, const std::vector<TypedVec> &cols, TypedValue &out,
                 std::string &err);

} // namespace nextpilot::plotscope::protocol
