# psproto v2：模板驱动的动态帧解码库

> 定位一句话：**用一份 JSON 模板声明帧布局，在运行时编译成段表，再把字节流解成 `channel → (t, v)`。不生成代码，不重编译。**
>
> 本文件是 `ng/libs/psproto`（现 `ng/libs/protocol`）的完善方案，含**表达力统计**、schema、流式解析器、编译器与代码生成路线。

| 项 | 值 |
|---|---|
| 状态 | 方案待拍板（2026-11-19） |
| 现状代码 | 2462 行：`ProtocolDocument`(839) / `ProtocolSpec`(317) / `SequentialCodec`(1053) |
| 硬约束 | 纯逻辑、无 QWidget、无 Dataset、无宿主；headless 可单测 |

---

## 1. 现状盘点：现在能描述什么

### 1.1 已支持

| 维度 | 能力 |
|---|---|
| 布局 | `layout: "sequential"`（顺序帧） |
| 字节序 | 文档级 `endian`，段级可覆盖（`bigEndian`） |
| 类型 | `u8..i64`、`f32/f64`、`bool`、`bcd`、`str/string/ascii/char` |
| 段属性 | `name`、`role`（sync/length/payload/checksum/data）、`unit`、`description`、`prefix`、`scale`、`offset`、`hidden`、`plot`、`value`（期望值）、`size`（常量或**表达式**） |
| 变长 | `size` 表达式可引用前序段（含 `counts`）→ 变长数组 |
| 变体 | `series.selector` + `variants[]`，按选择器值切不同段布局；`fallback: reject/raw` |
| 校验 | 谓词 `eq/ne/lt/le/gt/ge/in/range/mask`；序列校验 `crc8/16/32`、`sum8/16`、`xor8`、`parity`，带 `over` 范围；CRC 可自定义 `poly/init/xorout/refin/refout` |
| 表达式 | `+ - * / % & \| ^ << >>`、`abs/min/max/clamp`、字段引用（可带下标） |
| 时间戳 | `hasTimestamp/timestampExpr/timestampUnit(s/ms/us)/origin(first_frame)/raw/fallback` |
| 重同步 | 解帧失败时前进 1 字节重试（隐式扫描） |

### 1.2 明确不支持（关键短板）

1. **按分隔符读**：没有“读到 `\r\n` / `,` / `*` 为止”的能力 → ASCII 行协议几乎无法描述；
2. **文本数字**：`str` 段只能当字符串显示，不能 `"12.34" → 12.34` 进通道；
3. **字符串通道**：`str` 段不产通道（只用于预览与选择器）；
4. **位域**：段只能整字节对齐，没有 `bit_offset/bit_width`；
5. **派生通道**：`valueExpr` 只做校验，`hasValue` 的段反而**被排除出通道**；`scale/offset` 只能线性变换，非线性公式算不出通道；
6. **通用 CRC**：宽度被算子名写死（8/16/32），无 `crc24`（RTCM）、无 `crc64`；
7. **嵌套 / 重复组**：无 TLV、无子结构、无“按块长度跳读”；
8. **全流一次喂入**：`decodeSequential(bytes, doc)` 要求整块字节，流式分片要上层自己缓冲；无 `feedByte` / `feedChunk`。

---

## 2. 表达力统计（样本：24 个常见格式）

> 判定口径：**可** = 现有 schema 直接能写；**需补** = 补本文 §4 的能力后能写；**不可** = 属于容器/嵌套，不该塞进帧模板。

### 2.1 二进制（16 个）

| # | 格式 | 特征 | 判定 | 缺什么 |
|---|---|---|---|---|
| 1 | 自定义飞控帧（sync+len+id+crc） | 定长头+变长体+CRC16 | **可** | — |
| 2 | u-blox UBX | `B5 62` + class/id + len + payload + CK_A/B | **可** | — |
| 3 | MSP / 私有遥测 | 头+len+payload+校验 | **可** | — |
| 4 | Modbus RTU | 地址+功能码+len+数据+CRC16 | **可** | — |
| 5 | HID/传感器寄存器流 | 定长记录+寄存器地址变体 | **可** | — |
| 6 | MAVLink v2 | 10 字节头 + 24bit msgid + 变长 payload + CRC16 + CRC_EXTRA | 可（受限） | 需要外部常量表做 CRC_EXTRA |
| 7 | RTCM3 | `D3` + 10bit len + 12bit msg + CRC24Q | 需补 | 通用 CRC（24 位） |
| 8 | ARINC 429 | 32 位字：label/SDI/data/SSM/parity | 需补 | 位域或派生通道（`(v>>…)&…`） |
| 9 | CAN 2.0 / DBC 信号 | 8 字节按位切信号 | 需补 | 位域 |
| 10 | J1939 / NMEA 2000 | 29 位 id + 8 字节位域 | 需补 | 位域 + 长帧重组 |
| 11 | ADS-B / Mode S | 位域 + 跨字段推导 | 需补 | 位域 + 派生通道 |
| 12 | 1553B | 命令字/数据字位域 | 需补 | 位域 |
| 13 | SpaceWire RMAP | 命令+地址+数据，带嵌套 | 不可 | 嵌套 |
| 14 | PCAP | 文件容器 | 不可 | 容器 |
| 15 | MDF4 / BLF | 块嵌套容器 | 不可 | 容器 |
| 16 | PNG / MP4 | 分块嵌套 | 不可 | 容器 |

**小计：现成可写 5/16（31%）；补齐后 11/16（69%）；结构性不可 5 个。**

### 2.2 ASCII（8 个）

| # | 格式 | 特征 | 判定 | 缺什么 |
|---|---|---|---|---|
| 17 | NMEA 0183 | `$…*hh\r\n`，逗号字段 | 需补 | 读终止符 + 文本数字 |
| 18 | AIS（NMEA 封装） | 同 17 + 6bit 编码 payload | 需补 | 同上 + 位域 |
| 19 | SBS-1 | 逗号分隔定字段 | 需补 | 读终止符 + 文本数字 |
| 20 | CSV | 行+列分隔+表头 | 需补 | 分隔符切分 + 文本数字 |
| 21 | OEM7 ASCII 头 | `#HEADER,...;` | 需补 | 读终止符 + 文本数字 |
| 22 | 自定义 `K:12.3,P:4.5\r\n` | 键值行 | 需补 | 同上 |
| 23 | AT 命令 / 应答 | 行 + 前缀 + 可选字段 | 需补 | 同上 |
| 24 | JSON Lines | 结构化嵌套 | 不可 | 不该用帧模板（交给专门解析器） |

**小计：现成可写 0/8；补齐后 7/8（87%）。**

### 2.3 结论

- **总计约 40% 现成**（全部集中在二进制定长/变长帧）；
- **补齐 5 项能力后约 75%**（ASCII 从 0 拉到 87%）；
- 剩下 ~25% 是**容器格式与嵌套结构**（PCAP/MDF4/PNG/JSON），应当明确交给专用解析器，**不扩张本库去覆盖**——那会把一个干净的帧模板语言变成半吊子通用格式引擎。

**优先级由此确定：ASCII 是最大短板，位域是二进制第二大短板。**

---

## 3. 目标

1. 一份**可校验的 schema** + 一份**能照着写模板的 README**；
2. **模板编译一次 → 段表**，解码期零 JSON 遍历（学 binary-parser 的“运行时生成紧凑解析器”）；
3. **流式解析器**：`feedByte` / `feedChunk` / `finish`，内部残余缓冲；
4. 补齐 §2 暴露的 5 项表达力缺口；
5. 预留**代码生成**（C++ → Python → Java），生成物自包含、不依赖本库运行时。

---

## 4. 设计

### 4.1 模板 schema（两层校验）

**产物**：`ng/libs/psproto/schema/psproto.schema.json`（JSON Schema draft-07）+ C++ 内建校验器。

为什么两层：
- **JSON Schema** 给编辑器/CI/外部工具用（VSCode `$schema` 直接提示、自动补全、拼写检查）；
- **C++ `validate()`** 给运行时用（不引入 JSON Schema 依赖，保持 headless 纯逻辑），负责语义校验：段名唯一、role 唯一、表达式引用存在、size 表达式无环、变体 key 合法性。

schema 顶层结构：

```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "psproto 帧模板",
  "type": "object",
  "required": ["version", "name", "layout", "segments"],
  "properties": {
    "version":  { "type": "string" },
    "name":     { "type": "string" },
    "description": { "type": "string" },
    "layout":   { "enum": ["sequential"] },
    "endian":   { "enum": ["little", "big"] },
    "maxsize":  { "type": "integer", "minimum": 1, "maximum": 1048576 },
    "segments": { "$ref": "#/definitions/segments" },
    "series":   { "$ref": "#/definitions/series" },
    "timestamp":{ "$ref": "#/definitions/timestamp" }
  }
}
```

段（v2 扩充项标 ➕）：

```json
{
  "name": "roll",
  "role": "data",
  "type": "i16",
  "size": 1,
  "bit": { "offset": 0, "width": 16 },     // ➕ 位域（可选，缺省整字节对齐）
  "until": "\r\n",                          // ➕ 读到终止符（与 size 互斥）
  "scale": 0.01, "offset": 0.0, "unit": "rad",
  "derive": "temp_c * 1.8 + 32",            // ➕ 派生成通道（可引用前序段）
  "checks": [ { "op": "crc16", "over": ["body:0-10"] } ],
  "plot": true, "hidden": false
}
```

`type` 增加 ➕ `text_number`（把 ASCII 文本按十进制/十六进制解析成数值）。

### 4.2 README（用户视角）

`ng/libs/psproto/README.md` 结构固定为：

1. **30 秒上手**：最小二进制模板 + 最小 ASCII 模板，各配一行命令/一段 C++ 调用；
2. **模板速查表**：字段 × 类型 × 是否必填 × 默认值 × 示例（一张表覆盖全部键）；
3. **六种典型场景配方**：定长帧、变长数组、按 id 变体、ASCII 行、位域（CAN 风格）、时间戳换算；
4. **校验与调试**：`validate` 报错码表、`previewSingleFrame` 怎么看、常见坑（字节序、长度是否含头、CRC 覆盖范围）；
5. **从老 `.psproto` 迁移**：`frame_size/sync/fields` 旧方言 → 新 `segments` 的对照表。

### 4.3 编译成段表（核心性能改造）

现状：`decodeSequentialJson` 每帧都重新解析 JSON、重建段结构、重复求 size 表达式。

改造为两段式：

```cpp
struct Program;                                  // 不透明编译产物
bool compile(const QJsonObject &root, Program &out, std::string &err);   // 一次

// 编译后段表（内部结构，外部不依赖）
struct RtSeg {
    SegReader read;        // 具体读取器：u8/i16/f32/bcd/str/until/bitfield
    SizeFn size;           // 常量，或预编译表达式字节码
    std::vector<CheckFn> checks;
    uint32_t channelBase;  // 通道下标基址
    bool emit;
};
struct Program {
    std::vector<RtSeg> segments;             // 扁平段表
    std::vector<VariantTable> variants;      // 选择器 → 段表切片
    size_t fixedSize;                        // 全定长时可跳过长度计算
    // 表达式编译成字节码（栈式），不再走 AST 递归
};
```

验收口径：同一份 12 秒 ULog 级数据（3000 帧 × 16 段），改造后解码耗时至少降到原来的 **1/3**（现场测，不臆测）。

### 4.4 流式解析器

```cpp
class StreamParser
{
public:
    explicit StreamParser(const Program &program);

    // 两种输入粒度，同一状态机：
    bool feedByte(uint8_t b);                    // 单字节（便利接口）
    bool feedChunk(const uint8_t *data, size_t n);// 块（推荐主路径）
    bool finish();                               // 冲刷尾帧

    void setSink(FrameSink sink);                // 每解出一帧回调
    void reset();                                // 换文件复用
private:
    std::vector<uint8_t> m_residual;             // 半帧残余（唯一常驻缓冲）
};
```

纪律：
- **单字节不是主路径**：`feedByte` 内部并入残余缓冲后走块逻辑，避免逐字节虚调用；
- 常驻内存 = 残余缓冲 + 一帧，与文件大小无关；
- 解帧失败前进 1 字节重同步，与现状一致；
- 输出走 sink 回调，不攒整份结果（`decodeSequential` 保留为“整块便捷入口”，内部用 `StreamParser` 实现，行为不回归）。

### 4.5 补齐表达力（按优先级）

| 优先级 | 能力 | 设计 | 覆盖收益 |
|---|---|---|---|
| P0 | **行帧 + 读终止符** `until` | 见 §4.9：帧以 `\r\n` 结束，字段读到分隔符为止 | NMEA / 行式 ASCII |
| P0 | **文本数字** `text_number` / `time_hhmmss` | 见 §4.9：ASCII 片段转数值，含空字段策略 | NMEA / 行式 ASCII |
| P0 | **派生通道** `derive` | 表达式产通道（引用前序段、可用位运算）；取代 `valueExpr` 只校验的局限 | ARINC/ADS-B/CAN 手工解 |
| P0 | **位域** `bit.*` | 见 §4.8：段可声明位偏移/位宽/位序，跨字节不对齐 | CAN/J1939/1553/ARINC/DBC |
| P1 | **参数化 CRC** `op:"crc"` | 宽度显式（8/16/24/32/64）+ `poly/init/refin/refout/xorout` 全显式，`alg:"crc24q"` 可指向内含算法表；旧算子保留为别名 | RTCM3/任意标准 CRC |
| P1 | **帧脚本（AST 逃生门）** | 见 §4.7：校验/派生/选择器三处都能写一小段代码 | 非标校验、非线性换算、协议内自写 CRC |
| P2 | **字符串通道** | `str` 段可选产“枚举化数值通道”（值 → 枚举表下标） | 状态字、模式字段 |
| P2 | 嵌套/重复组 | **不做**，明确留给专用解析器 | — |

### 4.6 代码生成（S5，预留）

从 `Program` 直接生成自包含源码，**不依赖本库运行时**：

```
psproto-codegen --template foo.psproto --lang cpp   > foo_parser.h
psproto-codegen --template foo.psproto --lang python > foo_parser.py
psproto-codegen --template foo.psproto --lang java   > FooParser.java
```

生成物形态（C++）：

```cpp
namespace foo {
struct Frame { int64_t t_us; double roll; double pitch; };
class Parser {
public:
    void feed(const uint8_t*, size_t);
    std::function<void(const Frame&)> onFrame;
};
}
```

验收：**同一份模板、同一份字节，生成的 C++ 解析器与解释器输出逐字段一致**（golden 测试）。先 C++（与宿主同语言、可 embed），再 Python（脚本/分析），最后 Java。

---

## 4.7 脚本兜底：复用项目既有 Lua（放弃自建 AST）

> 需求：算子为主 + 脚本兜底，lib 内置常用算子；脚本能否直接用 Lua，省掉 AST。
> **结论：用 Lua。项目里已经有了，不必新造。**

### 4.7.1 已核实的事实

| 事实 | 位置 |
|---|---|
| Lua 5.4.4 静态库已入库 | `deps/lua/lib/liblua.a`，由 `tools/bootstrap-deps.sh` 提供 |
| Lua 引擎已跑通，有 `ps` 门面 | `ng/modules/script/LuaEngine.{h,cpp}`（212+38 行） |
| 目标由 module 层定义 | `ng/modules/script/CMakeLists.txt` 里 `ng_lua` IMPORTED target |
| **无沙箱** | `LuaEngine.cpp:165` 是 `luaL_openlibs(m_lua)`，`io`/`os`/`package`/`debug` 全开 |
| **无配额** | 没有 `lua_sethook`、没有 `lua_setallocf` |

所以不是“要不要引入 Lua”，而是“怎么把已有的 Lua 正确地接进解码路径”。

### 4.7.2 直接冲突：Lua 与 codegen 不能共存

第 5 条要求「生成 C++/Java/Python 源码给其他语言用」。Lua 方案在这里有一个**无法绕过**的冲突：

- Lua 是**完整语言**：table、闭包、metatable、协程、可变参数、动态类型。把任意 Lua 翻译成 C++/Java/Python，等于写一个 Lua 编译器——**工作量比自建 pscode 大一个量级，且永远不完备**；
- 让生成物依赖 Lua 运行时：C++ 要链 liblua，Java 要 JNI，Python 要 lupa/嵌入解释器——“自包含”随即破功。

**结论：含脚本的模板，不做 codegen。**

这不是缺陷，因为「算子为主」意味着脚本是极少数例外（见 §4.7.6 的算子表），codegen 覆盖率会远高于 95%。codegen 遇到脚本时**必须显式报错并列出段名**，绝不静默产出错误代码——静默降级是这一行最坏的失败模式。

### 4.7.3 三个必须补的护栏（现有 LuaEngine 一个都没有）

**护栏 1：沙箱。** 解码循环里跑模板内的脚本，而 `luaL_openlibs` 全开会给用户 `os.execute` / `io.open`。裁掉 `io`、`os`、`package`、`debug`、`load`、`loadstring`、`dofile`、`require`，只保留 `string`/`math`/`table`/位运算/协程外的纯计算库。

**护栏 2：配额。** `lua_sethook` count hook（每 1000 条指令检查）+ 单帧累计指令上限；`lua_setallocf` 限内存。没有这两条，一个 `while true do end` 就能卡死 GUI。

**护栏 3：类型语义。** Lua 5.4 的 integer 是 **int64 有符号**，`c = c << 8` 反复移位会溢出成负数。CRC 必须在脚本里显式掩码：

```lua
-- CRC-24Q（RTCM3）用 Lua 写
local c = 0
for i = frame.off("body"), frame.off("body") + frame.len("body") - 1 do
  c = (c ~ frame.byte(i)) << 8
  c = c & 0xFFFFFF                       -- 必须显式掩码，否则 int64 溢出为负
  for _ = 1, 8 do
    if c & 0x800000 ~= 0 then
      c = ((c << 1) ~ 0x1864CFB) & 0xFFFFFF
    else
      c = (c << 1) & 0xFFFFFF
    end
  end
end
return c == frame.seg("csum")
```

这正是 Lua 相对「定宽无符号」的代价：**语义靠约定 + 手工掩码，容易写错**。因为脚本是兜底、数量极少，这个代价可以接受；但不能让用户以为 Lua 里 `u32` 自动截断。

### 4.7.4 写法（三个挂点）

```jsonc
"checks":  [{ "op": "script", "lua": "…", "expect": true }]   // 返回 bool
{ "name": "temp_c", "derive": { "lua": "…", "unit": "C" } }    // 返回 number
"series":  { "selector": "…", "selectorLua": "…" }             // 返回 string/number
```

脚本可访问的最小 API（只读写帧，不碰宿主）：

| API | 说明 |
|---|---|
| `frame.len` | 帧字节数 |
| `frame.byte(i)` | 第 i 字节（0 基） |
| `frame.off(seg)` / `frame.len(seg)` | 段的字节偏移/长度 |
| `frame.seg(name)` | 段物理值（浮点） |
| `frame.raw(name)` | 段原始值 |

**性能纪律**：每份脚本按「模板 + 用途」`luaL_loadbuffer` **预编译一次**，之后每帧只 `lua_pcall`；禁止每帧 `luaL_dostring`。参数走 Lua 栈，不走 QVariant 转换。

### 4.7.5 内置算子：把「算子为主」落到实处

要真正让脚本变成极少数例外，光有 `crc` 参数化不够，得内置**标准算法表**，用户一个名字调用：

```jsonc
"checks": [{ "op": "crc", "alg": "crc16/modbus", "over": ["body..bodyEnd"] }]
```

首批内置（编译期按参数生成 256 项表）：

| 族 | 算法 |
|---|---|
| CRC-8 | SAE-J1850、MAXIM(Dallas)、ROHC、ATM、AUTOSAR、ITU |
| CRC-16 | CCITT-FALSE、XMODEM、KERMIT、MODBUS、USB、MAXIM、DNP、DECT-R |
| **CRC-24** | **OPENPGP、OS9、LTE-A、RTCM/24Q** |
| CRC-32 | ISO-HDLC、BZIP2、MPEG2、POSIX、JAMCRC、XFER、AUTOSAR、AIXM |
| CRC-64 | XZ、ECMA-182、GO-ISO |
| 非 CRC | sum8/16/32、xor8/16、parity、**Fletcher-16/32**、Adler-32 |

`alg` 与手填 `poly/init/refin/refout/xorout` 二选一；`alg` 优先。

### 4.7.6 依赖方向要动一处

`ng_lua` 现在定义在 `ng/modules/script/CMakeLists.txt`，而 psproto 属于 `ng/libs`（纯逻辑层，不能依赖 modules）。**`ng_lua` 的 IMPORTED target 必须上提到 `ng/CMakeLists.txt` 顶层**，让 libs 与 modules 平级共用。

否则会出现 `libs → modules` 的反向依赖，这条线一旦破，后面所有库都会照着破。

### 4.7.7 什么时候才回头做 pscode

只有当出现「**含脚本的模板也要求 codegen**」这个真实需求时才做。届时 pscode 的定位不是替代 Lua，而是「可编译的受限语言」——Lua 继续负责复杂脚本，pscode 负责需要生成源码的那部分。现在不做，避免 1900 行投入换取一个还不存在的需求。

---

## 4.8 位域：按位切信号

> 需求原话：「没有位域，这个要支持」。已从 P1 升为 **P0**。

### 4.8.1 为什么必须内置，不接受“靠表达式拼”

现状 `readElem()` 全部按整字节对齐读，`size` 最小粒度是 1 字节。用 `derive` 里 `(v >> 3) & 0x1F` 拼确实**能**凑出结果，但有四个代价：

1. **可读性崩坏**：DBC 一帧 8 字节带 30 个信号，就是 30 行魔法数偏移，改一个信号要重算一片；
2. **无法自动校验越界**：手算 shift 写错不报错，静默出垃圾数据；
3. **codegen 无法优化**：表达式是通用路径，提取不出“一次加载 + 一次移位”；
4. **DBC 无法直接导入**：`.dbc` 的 start bit/length/signed/scale 本来就与位域语义一一对应，绕道表达式就白丢了这张映射表。

内置后：模板里一行一个信号，校验器能查越界，编译期能生成最优提取器，DBC 可 1:1 映射。

### 4.8.2 数据模型：扁平位域段 + 容器段（不引入嵌套）

沿用现有扁平段表，位域段用 `bit` 指向一个**容器段**，不新增嵌套结构：

```jsonc
// 容器：CAN 报文 8 字节 payload，自身不产通道
{ "name": "payload", "type": "u8", "size": 8, "hidden": true }

// 位域信号：相对 payload 首字节起算
{ "name": "engine_rpm", "type": "u16",
  "bit": { "base": "payload", "offset": 8, "width": 16, "order": "msb0" },
  "scale": 0.25, "unit": "rpm" }
```

- `base`：缺省 `"frame"`（相对帧首字节）；可填段名（相对该段首字节）；
- `offset`：**位**偏移（不是字节），从 `base` 首字节起算；
- `width`：位宽 1–64；
- `order`：`lsb0`（默认）/ `msb0`，见 §4.8.3；
- 位域段**不再使用 `size`**，占用宽度由 `width` 决定；`type` 只用来定符号（`u16`/`i16`/`u32`…）；
- 位域段不得为 `role: sync/length/payload/checksum`（这些必须是字节粒度）。

这样做的理由：段表保持扁平，编译期不需要建树，`decode` 循环不用递归——与现有架构完全兼容。

### 4.8.3 位编号：三个维度必须分开（这是最容易翻车的地方）

业界的坑在于把三件事混成一个“位偏移”：

| 维度 | 含义 | 本库字段 |
|---|---|---|
| 字节序 | 多字节整数怎么拼 | `endian`（文档级）/ 段级覆盖 |
| **位序** | 位索引从 LSB 还是 MSB 起数 | `bit.order` |
| 位偏移 | 从容器首字节起算多少位 | `bit.offset` |

两种位序的精确定义：

- **`lsb0`**：位索引 `i` → 第 `i/8` 字节的第 `i%8` 位（bit0 在最右）。索引递增 = 向高位走，跨字节连续。
- **`msb0`**：位索引 `i` → 第 `i/8` 字节的第 `7 - (i%8)` 位（bit7 在最左）。索引递增 = 向低位走，跨字节连续。

### 4.8.4 与主流协议位编号的等价对照（已核对）

| 协议 / 工具 | 是否等价 | 对应配置 |
|---|---|---|
| DBC **Intel**（little-endian）信号 | 等价 | `order:"lsb0"`，`offset = start bit` |
| DBC **Motorola**（big-endian）信号 | 等价 | `order:"msb0"`，`offset = 字节*8 + (7 - startbit%8)` |
| J1939 / NMEA 2000 | 等价 | 同 Motorola，多帧需上层重组 |
| u-blox UBX 位标志字 | 等价 | `lsb0` + `width:1` 取单个 flag |
| ARINC 429 32 位字 | 等价 | 容器 `u32`（`order:"msb0"`），label/data/SSM 各一个位域 |
| 1553B 命令字 | 等价 | 同 ARINC，`msb0` 分字段 |

Motorola 等价性推导（写进文档防止后人改坏）：DBC start bit `s` 位于字节 `b=s/8`、位 `k=s%8`（MSB 优先编号），换算到 `msb0` 索引为 `b*8 + (7-k)`；信号向低位延伸时索引递增，`k=0` 之后正好落到下一字节 bit7，索引连续。**故 `msb0` 模型与 DBC Motorola 逐位等价。**

### 4.8.5 提取算法（编译期编成一个四元组）

编译期把 `(base, offset, width, order)` 折成：

```cpp
struct BitReader {
    uint32_t byteOff;   // 起始字节（相对 base）
    uint8_t  loadBytes; // 1..8
    uint8_t  shift;
    uint64_t mask;
    bool     msb0;
};
```

解码期：

```cpp
uint64_t raw = readUInt(p + byteOff, loadBytes, msb0 /* 拼装按 MSB 优先 */);
raw = (raw >> shift) & mask;
if (signed && width < 64) { raw = (uint64_t)((int64_t)(raw << (64-width)) >> (64-width)); }
phys = raw * scale + offset;
```

- `lsb0`：`loadBytes = ceil((offset%8 + width)/8)`，`shift = offset%8`；
- `msb0`：`shift = loadBytes*8 - (offset%8) - width`。

**约束（务实取舍）**：`width + (offset % 8) ≤ 64`。这条保证了跨字节时 `loadBytes ≤ 8`，一次 `readUInt` 就能取完，不需要 `__int128`（MSVC 没有）也不需要第二次读取。违背此约束时校验器直接报错，不做静默降级——位域越界静默降级是最坏的设计。

成本：一次 ≤8 字节 `readUInt`（常量长度，编译期可展开）+ 一次移位 + 一次 mask。比“每帧跑表达式”快，比逐位循环快得多。

### 4.8.6 与现有能力的关系

| 组合 | 行为 |
|---|---|
| 位域 + `scale/offset` | 支持（CAN 物理值 = raw*scale + offset，DBC 同语义） |
| 位域 + 有符号 | 支持，按 `width` 做符号扩展（不是按 `type` 宽度） |
| 位域 + `checks` | 支持（eq/range/mask 等，按物理值判） |
| 位域 + `derive` / 脚本 | 支持，位域值可被脚本引用 |
| 位域 + 变体 `series` | 支持（变体整体替换段表，offset 语义不变） |
| 位域 + `until`（ASCII） | **互斥**，校验报错 |
| 位域 + `counts` 数组 | **互斥**（数组请用字节粒度段 + `size` 表达式） |
| 位域 + `role` | 互斥（sync/length/payload/checksum 必须字节粒度） |

### 4.8.7 验收（硬标准，不接受“看起来对”）

1. 用 `cantools` 从一份真实 `.dbc` 生成期望解码结果，抽 **32 个信号**（含 Intel/Motorola 混合、非对齐、有符号、跨字节），与本库输出**逐位比对全部相等**；
2. 一份 8 字节 CAN 报文模板能解出 30+ 信号，模板内无一处手写 `>>`/`&`；
3. 越界配置（`width + offset%8 > 64`、`base` 不存在、位域与 `until` 同时出现）全部被 `validate` 拒绝，报错带段名与具体位范围；
4. ARINC 429 与 1553B 各一份模板可解，作为“非 CAN 场景”的回归。

---

---

## 4.9 ASCII：以 NMEA 为目标（不做通用 CSV 引擎）

> 需求原话：「ASCII 主要是想解析类似 NMEA 这样的」。

### 4.9.1 为什么收窄是对的

通用 ASCII 引擎（CSV 引号转义、JSON、键值对、多行续行）会把这个库从"帧模板语言"变成 **tokenizer**，代价是 schema 复杂度翻倍、codegen 三语言难等价，而收益只覆盖少数格式。

NMEA 一族用 **5 个原语**就能完整覆盖，且其中 `until` / `text_number` 是通用原语——将来真要做 CSV，是在同一套原语上加"引号转义"两个标志位，不是另起一套。

**因此本节范围：NMEA 0183（含厂商私有语句）+ AIS 封装（P2）。明确不做：CSV/JSON/键值对/多行续行。**

### 4.9.2 先承认现状不可用

现在 `tests/fixture/demo_nmea.psproto` 是**假的**：它把 `msgid` 当定长 5 字节，把 payload 当定长 4 字节的 `u8`，再用 `u16 × scale:0.1` 假装读出 `alt/hdop`。真实 NMEA 是可变长度、逗号分隔、ASCII 文本。**这份模板解不了任何真实日志**——这是现状的第一个 ASCII 坑，必须先说明白。

### 4.9.3 真实帧结构

```
$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n
└起始  └地址└时间 └纬度   └半球└经度...                    └校验└终止
```

- 起始符：`$`（0183）或 `!`（AIS 封装）；
- 地址：5 字符 = talker(2) + sentence(3)，如 `GPGGA`；
- 字段：逗号分隔，**可能为空**（`,` 之间什么都没有）；
- 校验：`*` + 2 位十六进制，算法是 **`$` 与 `*` 之间所有字符的 XOR8**（不是 CRC）；
- 终止：`\r\n`。

### 4.9.4 五个原语

| 原语 | 语义 | 为什么必须有 |
|---|---|---|
| `frame.terminator` | 帧结束符（`"\r\n"`），`finish` 时冲刷 | 行帧的边界，替代"猜长度" |
| `frame.starts` | 合法起始字节集合（`["$","!"]`） | 重同步：只在行首找起始符，不做逐字节暴力扫描 |
| 段级 `until` | 读到指定字节序列为止（**不含**该序列） | 字段变长的唯一表达方式 |
| 段级 `consume` | 读完再吞掉 N 个字节（通常是 1 个分隔符） | 一个字段一行，不必为每个逗号再写一段 |
| `type: text_number` | ASCII 片段 → 数值（`radix` 10/16，支持 `+/-`/小数/科学计数） | 字段里是 `"4807.038"`，不是二进制 |
| `type: time_hhmmss` | `hhmmss.ss` → 当日秒数（含小数） | NMEA 时间字段的专用格式，转数值才能当时间轴 |

`type: hex2` 用于两位十六进制校验字段（`*4A` → 74）。

### 4.9.5 字段模板（GGA 片段，真实可解）

```jsonc
{
  "name": "nmea0183",
  "layout": "sequential",
  "endian": "little",
  "frame": { "starts": ["$", "!"], "terminator": "\r\n" },
  "segments": [
    { "name": "start",  "type": "u8", "size": 1, "role": "sync", "value": "$" },
    { "name": "addr",   "type": "ascii", "size": 5, "hidden": true },   // GPGGA
    { "name": "sep0",   "type": "u8", "size": 1, "hidden": true, "consume": 0, "value": "," },

    { "name": "utc",    "type": "time_hhmmss", "until": ",", "consume": 1, "unit": "s" },
    { "name": "lat",    "type": "text_number", "until": ",", "consume": 1, "radix": 10, "empty": "nan" },
    { "name": "ns",     "type": "ascii", "until": ",", "consume": 1, "hidden": true },
    { "name": "lon",    "type": "text_number", "until": ",", "consume": 1, "empty": "nan" },
    { "name": "ew",     "type": "ascii", "until": ",", "consume": 1, "hidden": true },
    { "name": "fix",    "type": "text_number", "until": ",", "consume": 1, "empty": "skip" },
    { "name": "sats",   "type": "text_number", "until": ",", "consume": 1, "empty": "skip" },
    { "name": "hdop",   "type": "text_number", "until": ",", "consume": 1, "empty": "nan" },
    { "name": "alt",    "type": "text_number", "until": ",", "consume": 1, "unit": "m", "empty": "nan" },
    // … 水位/单位/差分龄期/站号，同构
    { "name": "bodyEnd","type": "u8", "size": 0, "hidden": true },       // 校验区间的右端点锚

    { "name": "star",   "type": "u8", "size": 1, "hidden": true, "value": "*" },
    { "name": "csum",   "type": "hex2", "size": 1,
      "checks": [ { "op": "xor8", "over": ["addr..bodyEnd"] } ] },
    { "name": "eol",    "type": "u8", "size": 2, "hidden": true, "value": "\r\n" }
  ],
  "series": {                       // 按地址切语句类型
    "selector": "addr",
    "payload": "bodyEnd",
    "fallback": "raw",
    "variants": [ { "value": "GPGGA", "name": "gga", "segments": [] },
                  { "value": "GPRMC", "name": "rmc", "segments": [] } ]
  }
}
```

关键点解释：

- **`until` + `consume: 1`** 一个字段一行，不为每个逗号单独写段；
- **`bodyEnd` 是零长度锚点**，给校验提供"从 addr 到最后字段末尾"的区间——见 §4.9.6；
- **`series` 按 `addr` 切变体**：GGA 与 RMC 字段结构不同，用变体而不是硬塞一个模板；
- **`empty`**：空字段（`,,`）常见，`nan` 表示产 NaN 点，`skip` 表示本帧该通道不产点。

### 4.9.6 校验范围必须支持"段区间"

现状 `over: ["name:from-to"]` 只能按**字节偏移**指定范围。NMEA 的 XOR 范围是"从地址字段首字节到最后字段末字节"，长度随内容变——偏移是运行期才知道的。

所以 `over` 增加区间形式：

```jsonc
"over": ["addr..bodyEnd"]     // 左闭右开：addr 首字节 → bodyEnd 首字节之间
"over": ["addr..lastField"]   // 也接受段名直接配对
"over": ["body:0-10"]         // 原字节偏移形式保留不变
```

实现上：段表编译期把 `..` 记成 `(startSegIndex, endSegIndex)`，解码期解析完这些段后就有实际字节区间，XOR/CRC 直接在该区间算。**顺序解析天然满足这个条件**，不需要额外机制。

### 4.9.7 重同步：按行首，不按字节

ASCII 流的错误重同步不能逐字节试（一个 100 KB 日志会退化）。规则：

1. 在残余缓冲里找最近的 `\n`（或 `frame.starts` 中任一字节）；
2. 从该位置重新按行解析；
3. 一帧内字段数不符 / 校验失败 → 丢弃整行，跳到下一个 `\n`。

这样错误成本是"一行"，不是"一字节一次重试"。

### 4.9.8 明确不做

| 不做 | 理由 |
|---|---|
| CSV 引号转义 / 内嵌逗号 | 属于 CSV 方言，NMEA 用不到 |
| JSON / 键值对 | 结构嵌套，应交给专用解析器 |
| 多行续行 | NMEA 无此需求 |
| 跨帧日期拼装（RMC 日期 + GGA 时间 → 绝对 UTC） | 属于多帧状态重组，超出帧模板职责；先出"当日秒数"，绝对时间由上层或脚本处理 |
| AIS 6bit armor 解码 | P2，可用脚本或专用类型，先不做 |

### 4.9.9 验收

1. 一份**真实** NMEA 日志（GGA+RMC+GSA 混合，≥500 行，含 CRC 错误行与截断行）能完整解析；
2. 逐行字段值与独立工具（`nmea-parser` 或自写 python 脚本）比对，数值字段全部相等；
3. 校验错误行被拒并计数，**不污染**通道；
4. 截断行（缺 `\r\n`）在 `finish` 时正确处理；
5. 空字段产 `NaN` 或跳过，行为符合 `empty` 声明；
6. 重同步成本为"丢一行"，100 KB 日志解析耗时与行数线性相关（不是二次）。

---

---

## 9. 强健性审计（2026-11-19，逐行核对）

> 你问的是"这个库强不强健"。我读完全部 2462 行后的判断如下——**不是"能描述多少协议"的问题，而是"错的时候你会不会察觉"的问题。**

### 9.1 三句话结论

1. **表达力**：现成可描述约 40%（集中在二进制定长/变长帧），补齐位域+ASCII 后约 75%（§2）。
2. **强健性**：当前实现有 **1 处会崩**、**7 处会静默错**、**4 处性能隐患**、**2 处设计级硬伤**。其中"静默错"比"会崩"更危险。
3. **最大的缺口是诊断与测试**：没有逐帧失败原因，没有 fuzz 测试。没有这两样，"强健"只是口头承诺。

### 9.2 P0：会崩（必须立刻修）

| # | 问题 | 位置 | 触发 | 后果 |
|---|---|---|---|---|
| 1 | `std::stoul` 解析 `poly/init/xorout` **无 try/catch** | `ProtocolDocument.cpp:426/428/430` | 模板写 `"poly": "0xZZ"` 或 `"abc"` | 抛 `std::invalid_argument`，**未捕获异常直接终止进程** |

这不是理论风险：`over` 的区间解析都做了合法性校验（`ProtocolSpec.cpp:176-189`），唯独这三个十六进制参数没有——**校验强度不一致**，是明确的漏点。

修法：换成不抛异常的自写解析（按前缀判断进制 + 逐字符校验），失败写 `err` 返回。

### 9.3 P1：静默错（比崩溃更危险，因为你看不见）

| # | 问题 | 位置 | 你看到的现象 | 实际发生的事 |
|---|---|---|---|---|
| 2 | **数组超 256 静默截断** | `SequentialCodec.cpp:974-976` | 通道名 `x[0]`…`x[255]` 都正常 | 第 257 个之后**被丢弃**，只 `++warnings`（一个裸计数，**没有段名、没有帧号**）。用户以为数据完整 |
| 3 | **`maxsize` 形同虚设** | `ProtocolSpec.cpp:311` 只在校验"固定段总和"；解码路径不查 | 模板声明 `maxsize: 64` | 单段可达 **1 MB**（`evalSize` 硬上限，`:199`），10 个段可解 10 MB 帧。**声明与实际不符** |
| 4 | `str` 段不产通道 | `:441`、`:968` | 面板上只有数值通道 | ASCII 文本字段解出来后**直接丢弃**（选择器能用，但值不出现） |
| 5 | `hasValue` 段被排除出通道 | `:971` | 配了 `valueExpr` 反而更少通道 | 校验值/派生值**拿不到**，只能用于判真假 |
| 6 | **无逐帧失败原因** | `ProtocolDecodeResult` 只有 `ok/error/channels/rejectedFrames/warnings` | 只看到"拒绝了 300 帧" | **不知道第几帧、哪个段、什么原因**。帧级失败被 `++rejected; ++off; continue` 吞掉（`:1010`） |
| 7 | NaN/Inf 不拦截 | `readElem` 返回 NaN → 直接进通道 | 曲线出现断点/异常值 | 统计、峰值、导出全部被污染，且**无法溯源到源帧** |
| 8 | `count=0` 语义模糊 | `evalSize` 要求 `v>0`，`size:0` 直接判非法 | 空字段无法表达 | NMEA 空字段（`,,`）在现有架构里**写不出来**，这也是 §4.9 要新增 `empty` 策略的原因 |

`over` 引用未知段**有**校验（`ProtocolSpec.cpp:182`）——这一点做对了，可以作为其他字段的参考标准。

### 9.4 P2：性能隐患（不致命，但会让你误判"库很慢"）

| # | 问题 | 位置 | 后果 |
|---|---|---|---|
| 9 | 重同步逐字节 `off += 1`，无 sync 快速跳转 | `:645`、`:1010` | 纯噪声数据最坏 **O(n²)**；100 MB 误配模板会跑到不可接受 |
| 10 | 每帧重建两个 `std::map<std::string,…>` + 键拼接 | `:646-652`、`:974` | 热路径每帧十几次堆分配，KV 哈希开销远超解码本身 |
| 11 | series 路径 payload 被复制成 `std::vector<double>` | `:459`、`:684` | 1 字节 → 8 字节，**内存膨胀 8 倍**，与 fastplot 的零拷贝原则相反 |
| 12 | `decodeSequentialJson` 每次重新 JSON 解析 + validate + parseDocument | `:1029-1052` | 若上层每帧调一次，就是灾难；当前 API 没有"编译一次"的概念（这正是 S2 要解决的） |

### 9.5 P3：设计级硬伤（修 bug 解决不了，要改类型）

**硬伤 1：double 承载 u64 → 高精度时间戳必失真。**

全链路（表达式、通道、时间戳）都是 `double`，而 `double` 只在 2^53（≈9.0e15）内精确。纳秒时间戳 ~1.7e18，此刻 double 的 ULP 已是 **256 ns**。也就是说：

- u64 纳秒时间戳解出来就和原始值差 200+ ns；
- u64 计数器/大 ID 超过 2^53 直接错。

这不是"精度差一点"的问题，是**"这个库在 u64 域上不可信"**。**解法见 §10**（不是换成默认 i64，而是类型化 + 精确旁路；`PS_VALUE_U64` 早已在 C ABI 声明，只是存储没兑现）。

**硬伤 2：没有 fuzz，等于没有强健性承诺。**

现有测试是"正确路径"测试（fixture 能解对）。缺的是"错误路径"：随机字节流、畸形模板、极端尺寸。上述 P0/P1 的绝大多数问题，**一个 10 分钟 fuzz 就能全暴露**。

### 9.6 优化建议（按性价比排序）

| 优先级 | 动作 | 成本 | 收益 |
|---|---|---|---|
| 立刻 | `stoul` 换安全解析（P0-1） | 半天 | 消除唯一的崩溃点 |
| 立刻 | 加 `Diagnostics`：帧号 + 段名 + 原因，取代裸计数（P1-6） | 1 天 | 从"猜"变成"看得见"，是后续所有调试的地基 |
| 立刻 | `count` 截断显式报错带段名（P1-2）；`maxsize` 真正强制（P1-3）；NaN 显式策略（P1-7） | 1 天 | 消除三处静默数据损坏 |
| 立刻 | 加 fuzz 测试（随机字节 + 畸形模板） | 1 天 | 把"强健"变成可验证的事实 |
| S2 | 段表编译（消灭 P2-10、P2-12） | 见 §4.3 | 性能 3× 起 |
| S2 | 重同步按 sync 字节跳转（P2-9） | 1 天 | 从 O(n²) 到 O(n) |
| S2 | payload 零拷贝视图（P2-11） | 2 天 | 内存 8× → 1× |
| S2 | `i64` 通道类型（P3-1） | 2 天 | u64 域可信 |

### 9.7 强健性验收（建议直接进 CI）

1. **Fuzz 10 分钟**：随机字节流 + 随机模板，要求不崩、不超时（单次 ≤ 5 ms）、峰值内存 ≤ 2× maxsize；
2. **畸形模板集（≥100 份）**：全部返回结构化错误，**零异常抛出**、零崩溃；
3. **单帧诊断**：任一失败帧能给出 帧号 + 段名 + 期望值/实际值；
4. **静默丢数据零容忍**：任何"丢弃/截断/跳过"都必须出现在 `Diagnostics` 里，不得只有一个计数器；
5. **u64 精度**：`0xFFFFFFFFFFFFFFFE` 级别的值必须原样解出（当前会失败）。

---

---

## 10. 数值类型：值保持原类型，不做"统一 double"

> 需求原话：「字段原来是什么类型就什么类型呀，为什么一定要强调转为统一的」。
> **这个判断是对的。我上一版写"double 主路径 + raw 旁路"是妥协，会造成两份真相。**

### 10.1 为什么统一到 double 是错的

不是"精度差一点"，而是**类型是语义的一部分，抹掉类型等于抹掉语义**：

| 抹掉类型后 | 后果 |
|---|---|
| 校验 | `u64` 常量匹配在 double 域可能**判错真伪**（两个不同 u64 相等）→ 好帧判坏、坏帧判好 |
| 派生 | `u64 * 2` 在 double 域溢出成 1.8e19 的模糊值，再转回整型全错 |
| 导出 | 导出的"原始值"已经不是原始值 |
| 溯源 | 只能看到"一个浮点数"，不知道它原本是 `u16` 还是 `f32` |
| 显示 | 一个 32 位计数器显示成 `4.2949673e9`，用户无法判断是否越界 |

所以段声明 `type: "u64"`，通道就必须是 `u64`，**不需要、也不该有一个"统一的 double 视图"**。

### 10.2 但"统一"本身不是问题，"统一到有损表示"才是问题

这里要区分**轴**和**值**：

| | 是否该统一 | 统一到什么 |
|---|---|---|
| **时间轴** | **必须统一** | `i64` ticks（每秒/毫秒/微秒/纳秒的整数刻度） |
| **值** | **不该统一** | 各按段声明的原类型 |

理由：时间轴是坐标系，不统一就没法对齐、没法画、没法比较——但统一到 `i64` 是**无损**的（整数刻度）。统一到 `double 秒`才是有损：纳秒墙钟 `1.7e18` 此刻 ULP 已是 256 ns。

**一句话：轴上必须统一，但统一到无损的整数；值上不统一，各是各的类型。**

### 10.3 设计：一个通道一份数据，类型由声明决定

```cpp
// 每个通道按段声明的类型存，不做多份副本
struct ChannelBuffer
{
    ValueType type;                        // 声明即类型，只读不改
    std::variant<
        std::vector<std::uint8_t>,  std::vector<std::uint16_t>,
        std::vector<std::uint32_t>, std::vector<std::uint64_t>,
        std::vector<std::int8_t>,   std::vector<std::int16_t>,
        std::vector<std::int32_t>,  std::vector<std::int64_t>,
        std::vector<float>,         std::vector<double>,
        std::vector<std::string>
    > data;
};

struct ProtocolChannel
{
    std::string name, unit;
    ValueType   type;        // 与 data 的实际类型一一对应，校验期断言一致性
    std::variant<…> v;       // 一份数据，无旁路、无双写
    std::vector<std::int64_t> t;   // 时间：i64 ticks（唯一统一项）
    std::int64_t tTicksPerSecond;  // 刻度换算，消费者自己转
};
```

三条纪律：

1. **只有一份数据**。不做 `v` + `rawU` 双写——上一版那样做会出现"两份真相"，用户和下游都不知道该信哪个，这是最坏的设计；
2. **`type` 与 `data` 的实际类型强绑定**，`validate` 阶段就断言，防止声明与存储不一致；
3. **`phys`（scale/offset 之后）怎么办**：不额外存一份。当 `scale != 1 || offset != 0` 时，段的值和物理值确实不同——这时**段自身的声明类型就该是物理值的类型**（声明 `f32` 就是 `f32`）。`scale/offset` 是"从 raw 到 phys 的换算公式"，不是"两个通道"。需要 raw 的场景（如导出原始字节）走 `rawBytes`，不再造第二个 typed buffer。

### 10.4 转换点唯一：消费端，不在解析端

```
解析(psproto) ── 原类型，无损 ──► 消费端(需要 double 时才转)
```

- 画图：`fastplot` 要 `double` → 在喂图那一刻转，**单点、显式、可测**；
- 统计：按原类型算（整型算整型，浮点算浮点），需要时再转；
- 导出 CSV：原始值按原类型格式化（`uint64` 不打成科学计数法）；
- 校验：**永远在原生类型域**，这是正确性问题（§10.6）。

对比上一版的错误：把转换藏在解析输出里，精度损失发生在"你还没决定要不要精确"的时候。

### 10.5 表达式求值：跟着段类型走，不设"统一入口"

`evalExpr` 现在 `double` 一路到底（172 行），必须重写为类型化：

| 运算 | 规则 |
|---|---|
| `u64 op u64` | `u64`（除法/取模按无符号） |
| `i64 op i64` | `i64` |
| `u64` 与 `i64` 混合 | **编译期报错**，要求显式 `toI64()` / `toU64()` |
| 整型 op 浮点 | **仅允许显式 `toF64()`**，隐式转换 = 模板错误 |
| 比较 | 同类型才允许 |
| 位移/位运算 | 只在整型域；移位量超位宽时行为显式定义，不传播 UB |

语义对齐 Rust。这与"值保持原类型"是同一件事的两面：**如果表达式里偷偷转 double，段类型保持了也没用**。

### 10.6 校验必须在原生类型域（正确性，不是精度）

`checkFrame` 现在是 `if (v != want)` 的 **double 比较**（`SequentialCodec.cpp:220-225`）。后果：

- `u64` 常量匹配可能判错真伪；
- CRC 状态类大数直接比错 → **把好帧判成坏帧，或把坏帧当成好帧**。

这条与 S2 的段表编译一起做，不能等下游改造。

### 10.7 下游 Dataset 的正确姿势

`Dataset::Series::value` 现在是 `QVector<double>`，但 `Series` 已经有 `ValueType type` 字段（`Dataset.h:92`、`ValueType` 枚举 `:39` 已含 `U64/I64/U32…`）。也就是说**core 层早就打算支持类型，只是存储没兑现**。

处理顺序：

| 步 | 内容 | 为什么可以分步 |
|---|---|---|
| 第一步（S2） | psproto 输出 typed；表达式类型化；校验按类型；时间轴 i64 ticks | 这是**解析正确性**，不做则数据从源头就错 |
| 第二步（独立批次） | `Series::value` 改 typed（兑现 `PS_VALUE_*`），统计/LOD/抽稀/导出按类型走 | 这是**存储与显示**，波及 fastplot 全链路，风险高，可晚做 |

**中间态是可接受的**：psproto typed → 入库时按 `Dataset` 当前能力转 double（一次、显式、可记录"此处有损"），而不是解析时就模糊掉。区别在于**有损点是否可定位**。

### 10.8 验收

1. `0xFFFFFFFFFFFFFFFE` 级 u64：解出值、常量校验、导出**三者精确**；
2. 通道里**只有一份数据**：`sizeof(channel)` 与单类型 vector 同量级，不存在双写；
3. 纳秒时间戳 `1700000000123456789`：相对时间与手算差 **≤ 1 ns**（先 i64 相减，再换算）；
4. 表达式：`u64 * 2` 不丢位；`u64 + i64` 明文报错；`u32 + 1.5` 明文报错；
5. 所有 `f32/f64` fixture 输出与现在**逐位相同**（零回归）；
6. `type` 与 `data` 类型不一致时，`validate` 直接拒绝（防声明与存储脱节）。

---

## 5. 分批与验收

| 批次 | 内容 | 验收 |
|---|---|---|
| **S1** | JSON Schema + README + 现状模板迁移（`demo*.psproto` 全部通过新校验） | schema 可被 VSCode 识别；7 个 fixture 全部 `validate` 通过；README 能照着写出新模板 |
| **S2** | `compile()` → 段表 + 表达式字节码；`decode` 改走段表 | 行为与现状逐字段一致（回归测试）；3000 帧基准耗时 ≤ 1/3 |
| **S3** | `StreamParser`（feedByte/feedChunk/finish/setSink/reset） | 单字节逐喂 与 整块喂 结果一致；跨块半帧正确；尾帧冲刷正确 |
| **S4a** | 表达力补齐：`until` / `text_number` / `derive` / **位域（§4.8）** / 参数化 CRC（含 24 位） | NMEA、CSV 可解；**CAN DBC 32 信号与 cantools 逐位一致**；ARINC429/1553B 可解；RTCM3 可解；老模板不回归 |
| **S4b** | **Lua 兜底接入**：沙箱裁剪、指令/内存配额、脚本预编译、`frame` API、编辑器脚本变量面板 | 用 Lua 写 CRC24Q 与内置 `alg:"crc24/rtcm"` 结果**逐位一致**；`while true` 被配额中断；`os.execute` 不可用；错误带行号 |
| **S4c** | codegen 处理含脚本模板 | 遇到 `lua` 节点**显式报错并列出段名**，不静默降级 |
| **S5** | 模板 codegen（帧结构 → 解析器源码） | 生成的 C++ 解析器与解释器逐字段一致 |

---

## 6. 需要拍板的五个点

1. **旧 API 兼容**：保留 `decodeSequential(bytes, doc)` 作为整块便捷入口（内部走 StreamParser），还是直接切到新接口、老调用点全改？
   建议：**保留**，行为不变，降低上层迁移风险。

另外：**codegen（S5）是否本批就做**，还是先做到 S4c 再单独一批？
建议：**分两批**。三语言 golden 验收成本高，和表达力补齐混排会拖慢主线。

---

## 7. 红线

- 不引入 JSON Schema 运行时依赖（schema 文件给工具用，C++ 用自带校验）；
- 不生成代码也要能解析（解释器是主线，codegen 是附加物）；
- 解码期零 JSON 遍历、零 AST 递归（编译成段表 + 字节码）；
- 常驻内存与文件大小无关（只有残余缓冲 + 一帧）；
- 容器格式与嵌套 TLV **明确不覆盖**，不为“装得像通用引擎”牺牲简洁；
- `str` 不产数值通道时不得伪造数值，避免污染数据集；
- **脚本只能挂在校验/派生/选择器三处，不得用来描述帧布局**；脚本不得含 `while`、递归、I/O、堆分配。

## 8. 已定 / 未定

| 项 | 状态 |
|---|---|
| 位域内置（`bit.base/offset/width/order`） | **已定**（§4.8） |
| 位序 `lsb0`/`msb0` 双模型，对齐 DBC Intel/Motorola | **已定**（§4.8.4 有等价性推导） |
| 位域约束 `width + offset%8 ≤ 64`（避免 `__int128`） | **已定**（§4.8.5） |
| ASCII 目标 = NMEA 0183 + AIS(P2)，不做 CSV/JSON | **已定**（§4.9） |
| 脚本兜底 = 项目既有 Lua 5.4（不做自建 AST） | **已定**（§4.7） |
| 含脚本模板不做 codegen（显式报错） | **已定**（§4.7.2） |
| CRC = 内置算法表为主 + Lua 兜底 | **已定**（§4.7.5） |

| **时间戳 = i64 ticks（不转 double 秒）** | **已定**；落地见 [S0 施工方案](psproto-s0-plan.md) |
| **保留 `decodeSequential`**（签名不变，内部后续改走 StreamParser） | **已定** |
| 旧 API 是否保留 | ~~待拍板~~ |
