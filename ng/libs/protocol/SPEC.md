# psproto v3 规范

适用实现：`ng/libs/protocol`。`psproto.schema.json` 是机器可读版本，本文是语义说明。
v3 是**重构**，不是兼容升级：旧键一律报"未知键"，没有迁移期。

---

## 0. 定位与边界

psproto 描述**字节流 / 文本流怎么变成通道**。它不管绘图、告警、单位换算之外的物理语义。

- 属于协议层：字段切分、位域、重复、约束校验、数值变换、派生量、时间戳。
- 不属于协议层：Y 轴范围、告警阈值、图表样式——那是绘图的事。
- 一条原则：**能在已有键的一个取值里表达，就不要新增键。**

判断标准：能描述"一段字节 → 一帧 → 若干字段"就归 psproto；需要"先读索引表再跳着读"的归上层容器解析器。

---

## 1. 顶层 14 键

| 键 | 必填 | 说明 |
|---|---|---|
| `name` | ✓ | 协议名。通道前缀固定取 `frame.`，与协议名无关 |
| `mode` | ✓ | `bin` / `text`，必填，无 mixed |
| `segments` | ✓ | 段表，至少一段 |
| `description` | | 人类可读说明 |
| `layout` | | `sequential`（默认，顺序读） |
| `endian` | | `little`（默认）/ `big`。只跟顶层走，段级不再有 endian |
| `maxsize` | | 单帧字节上限，默认 4096 |
| `term` | | 帧尾候选集（字符串或数组）；写了就硬校验 |
| `terminator` | | `term` 的同义写法；两者同时出现时 `term` 优先 |
| `blocks` | | 块定义表，见 §4 |
| `derived` | | 派生量，见 §5 |
| `series` | | 变体分发（`selector` + `variants`） |
| `types` | | 类型别名：`type/unit/transform/expect` 的复用 |
| `timestamp` | | 时间戳提取（`expr`/`unit`/`origin`/`raw`/`fallback`） |

没有 `psproto`、`version` 键。二者都算未知键。

---

## 2. bin 段：只允许 10 个键

```
name / description / unit / output / type / count / transform / expect / on_mismatch / if
```

`name`、`type` 必填，其余可选。出现任何其它键（`scale` `offset` `range` `checks` `digest`
`length` `derive` `enum` `as` `bit` `until` `repeat` `emit` `hidden` `plot` …）都是**未知键错误**，
不静默忽略、不做兼容转换。

### 2.1 `output`

`channel`（默认，出通道）/ `aux`（参与表达式与校验、不出通道）/ `none`（只占位、只校验）。

### 2.2 `on_mismatch`

`reject`（默认，整帧失败）/ `warn`（出 NaN 通道 + 明细）/ `skip`（不出通道 + 明细）。

### 2.3 `if`

条件表达式，不成立时该段**不存在**：不消耗字节、不出通道、槽位保持空。

---

## 3. `count` 与 `type`

### 3.1 `count` 三态

| 写法 | 含义 |
|---|---|
| 整数 `3` | 恰好 3 个元素 |
| 字符串表达式 `n` / `hdr.len` | 运行期求值 |
| `"*"` | 读到帧尾；**全局唯一**，其后只允许固定长度的尾部段（给帧尾 CRC 留位） |

段占字节数 = `count × type 字节宽`（位域段恒为 0）。

### 3.2 `type`

四类取值，同一个键位：

1. **内置类型**：`u8 i8 u16 i16 u32 i32 u64 i64 f32 f64 bool bcd char string str number num rest time`
2. **`bit` / `rbit`**：位窗口，宽度与起始位写在 `count` 的 `"宽@偏移"` 形式（§3.3）
3. **`blocks` 里的块名**：整体引用一个块（§4）
4. **`types` 里的别名**：复用别名的类型 / 单位 / 变换 / 约束

### 3.3 位域：`bit` / `rbit` + `count:"宽@偏移"`

```jsonc
{"name": "id",   "type": "rbit", "count": "29@0"}
{"name": "rpm",  "type": "rbit", "count": "16@8", "transform": "self*0.25"}
{"name": "temp", "type": "bit",  "count": "8@24"}
```

- **位序靠类型值表达**：`bit` = lsb0（字节内从低位起数，Intel）；`rbit` = msb0（从高位起数，Motorola）。不额外占键。
- **偏移编号两种位序完全一致**：`@8` 永远是"从帧起点数的第 8 个位槽"。`rbit` 只改字节内取位方向与拼装顺序。
  - 同一字节 `12 34`、`count:"16@0"`：`bit` → `0x3412`，`rbit` → `0x1234`。
- 位域段**不推进字节游标**，偏移默认相对帧起点。
- 同帧内 `bit` 与 `rbit` 可自由混用。
- 位域取值按 `u64` 无符号；符号位域用 `transform` 表达（如 `self-16*(self>=8)`），不新增类型值。

---

## 4. `blocks`：字段组

```jsonc
"blocks": {
  "rec": { "repeater": "*", "fields": [
      {"name": "a", "type": "u16"},
      {"name": "b", "type": "u8"}
  ]},
  "flags": { "of": "u16", "fields": [
      {"name": "hi", "type": "rbit", "count": "4@12"},
      {"name": "lo", "type": "bit",  "count": "12@0"}
  ]}
},
"segments": [ {"name": "payload", "type": "rec"}, {"name": "f", "type": "flags"} ]
```

| 形态 | 判定 | 行为 |
|---|---|---|
| 顺序块 | 无 `of` | 字段按序读字节；每个字段必须**定长** |
| 位域块 | 有 `of` | 先读一个 `of` 类型的容器值，再按字段 `宽@偏移` 切位；**偏移相对容器起点** |
| 组重复 | 有 `repeater:"*"` | 这组字段整体重复到帧尾；组宽 = 块内字段定长之和（位域块为容器宽度） |

规则：

- 块只允许一层：块内字段不能再引用别的块。
- 组重复写在**块自己**的 `repeater` 上，段不再写 `count:"*"`（重复不占字段数）。二者同时出现即报错。
- 组内字段不是定长 → 报错，不静默按 1 处理。
- 段引用块时 `count` 只能是正整数常量（摊成多份实例），与 `repeater` 互斥。

---

## 5. `derived`：顶层派生量

```jsonc
"derived": [ { "name": "sum", "expr": "a + b", "unit": "x", "output": "channel" } ]
```

- 不读字节、不占空间、不参与帧边界。
- 按数组顺序求值，可引用**前序派生**。
- 名字不得与段重名。
- 表达式里引用**块内字段用裸字段名**（`a`，不是 `h.a`——带点的写法不是合法标识符）。同名冲突以最早出现的块实例为准。
- 引用其它段取的是**输出值（变换后的物理量）**；只有本段的 `self` 指原始值。

---

## 6. `transform`：数值变换

两种写法：

1. **字符串表达式**，`self` 指本段原始值：

```jsonc
{"name": "rpm", "transform": "self*0.25"}
{"name": "t",   "transform": "(self-32)*5/9"}
```

2. **算子数组**（多步组合）：

```jsonc
"transform": [ {"op": "scale", "k": 0.25}, {"op": "clamp", "min": 0, "max": 8000} ]
```

关键语义：

- 能折成 `self*k + b` 的表达式在**编译期**折进 `scale/offset`，走零开销路径；折不了的留表达式逐帧求值。
- **transform 表达式一律按浮点算**：`(self-32)*5/9` 里的 `5/9` 不会走整型整除（否则恒为 0）。

---

## 7. `expect`：约束

数组，逐项 **AND**，判的是**原始值**（变换之前）。

```jsonc
"expect": [ 53 ]
"expect": [ ">63", "[36,427)", "{1,3,5}" ]
"expect": [ "self+3" ]
"expect": [ { "op": "crc8", "over": "a:b", "poly": 179 } ]
"expect": [ { "op": "in", "values": [1,2], "labels": ["OFF","ON"] } ]
```

### 7.1 界面写法 → 结构

| 写法 | 含义 | 落成 |
|---|---|---|
| `53` | 等于 53 | `{op:"eq", value:53}` |
| `>63` `<10` `>=1` `<=9` | 单侧比较 | `gt/lt/ge/le` |
| `[36,427)` | 区间（左闭右开） | 拆 `ge` + `lt` **两条** |
| `{1,3,5}` | 枚举集合 | `{op:"in", values:[...]}` |
| `self+3` | 与本段值比较（省略左操作数默认 `self`） | 表达式比较 |
| `{1=OFF,2=ON}` | 带标签的枚举 | `{op:"in", values, labels}` |

**没有 `range` 这个 op**：开闭区间一律拆成 `ge/lt`。

### 7.2 op 清单（20 种）

- 判值：`eq ne lt le gt ge in mask`
- 摘要/校验和：`xor8 sum8 crc8 crc16 crc16_modbus crc16_x25 crc24q crc32 crc32c nmea_xor parity`
- 脚本：`script`（Lua）

摘要类的参数（`poly`/`init`/`refin`/`refout`/`xorout`/`over`）写在结构里，不在界面语法里编。

---

## 8. 通道命名

| 场景 | 名字 |
|---|---|
| 普通段 | `frame.<段名>` |
| 段有多个元素 | `frame.<段名>[i]` |
| 块内字段 | `frame.<实例>.<字段>` |
| 组重复块的第 i 轮 | `frame.<实例>[i].<字段>` |

索引插在**块实例名之后、字段名之前**：`payload[0].a`，不是 `payload.a[0]`。

---

## 9. 硬错清单（`validate()` 必报）

1. 未知键（顶层、段、块、派生、变体，任何层级）
2. `name` / `type` 缺失；`unit` / `description` 写成数字
3. `count` 非法：0、负数、`"*"` 出现两次、`"*"` 之后还有变长段
4. `bit`/`rbit` 的 `count` 不是 `"宽@偏移"`，或宽度/偏移越界
5. 顺序块里出现 `bit`/`rbit`；位域块里出现非位域字段；位域块子字段区间重叠或越界
6. 组重复块内字段非定长；`repeater` 与段 `count` 同时给
7. 表达式引用未知符号、引用后序段
8. 块名 / 段名 / 派生名重复
9. 固定总字节超过 `maxsize`
10. `expect` 的 op 参数不全（如 `crc8` 缺 `poly`）

---

## 10. `text`

text 段按 **扩展 scanf** 方式顺序消费：段与段共用同一条字符流，上一段读完的位置就是下一段的起点。

**11 个键**：`name` `description` `unit` `output` `format` `count` `transform` `expect` `on_mismatch` `if` `delimiter`

`format` 直接写扩展 scanf 语法，不写 `oct`/`string` 这类类型名：想读字符串就写 `%s`，想读八进制就写 `%o`。

### 10.1 `format` 语法

| 写法 | 含义 |
|---|---|
| `%d` `%u` | 十进制有/无符号 |
| `%x` `%X` | 十六进制（可带 `0x`） |
| `%o` | 八进制 |
| `%b` | 二进制 |
| `%f` `%e` `%g` | 浮点；`%e`/`%g` 收科学计数 |
| `%c` | 单字符，**不吃分隔符** |
| `%s` | 读到 `delimiter` 或空白为止 |
| `%[^,]` `%[^;\n]` | 自定义终止字符集 |
| `%t` | 时间 `hh:mm:ss[.frac]` |
| `%T` | ISO 时间 `yyyy-mm-ddThh:mm:ss` |
| `%*d` `%*[^,]` | 读了但丢弃 |
| `%5d` `%10s` | 定宽 |
| 普通字符 `$GPGGA,` | 必须逐字匹配，不匹配走 `on_mismatch` |

### 10.2 `count`

整条 `format` 的重复次数，与 bin 同义（元素数）：

- `{"format":"%d","count":8}` ＝ `%d,%d,%d,%d,%d,%d,%d,%d`
- `{"format":"%c","count":4}` ＝ `%c%c%c%c`（不跳分隔符）
- `count:"*"` ＝ 读到本帧尾（`terminator` 之前），全局唯一
- `count:"n"` ＝ 表达式，**逐帧求值**，可引用前面段已解出的字段（`"n"` 就是段名）；
  求值结果必须是正整数，否则整帧判坏值。

### 10.2.1 `transform`

text 段与 bin 段共用同一套变换，两种写法等价：

```json
{"name":"t","format":"%f","transform":"(self-32)*5/9"}
{"name":"t","format":"%f","transform":[{"op":"scale","k":0.5556},{"op":"offset","b":-17.778}]}
```

- 字符串 = 数学表达式，`self` 指本段刚读出的原始值，逐帧按浮点求值；
- 能折成 `self*k+b` 的在编译期折进 scale/offset，折不了的留表达式求值；
- 数组 = 算子链（`scale` `offset` `abs` `round` `clamp`），按顺序施加；
- `sexagesimal` 是"按文本切"的算子，它在数值解析阶段完成（`%f` 读 `4807.038` → `48.1173`），
  和表达式不冲突，可以并存。

### 10.3 `delimiter`

元素之间的分隔符，也是 `%s` 的终止集合，默认 `,`。`%c` 不消费分隔符。

### 10.4 帧首定位（硬要求）

text 不是"在流里直接 scanf"：**必须先定位帧首才读**，否则垃圾字节会被读成字段。

- 定位依据二选一：首段给出**字面锚点**（`{"format":"$"}`、`{"format":"$GPGGA,"}`，或首段 `expect` 的 `eq` 值），
  或顶层给了 `terminator` 按记录切分；
- 两者都没有 → 编译期直接报错：`text 协议必须先能定位帧首`；
- 锚点之前的字节计入 `skipped`，不算错误；
- 读一条记录时，若中途又见到锚点，当前候选作废并按 `RecordAborted` 计（与 NMEA 行为一致）。

### 10.4.1 校验和（xor8 / sum8 / crc8 / crc16 / crc32 / parity）

text 的校验写在 `expect` 里，算的是**记录的连续原始字节**（含中间的逗号），不是字段值拼回去：

```json
{"name":"check","format":"%x","output":"aux",
 "expect":[{"op":"xor8","over":"msgid:station"}]}
```

- `over` 用 `a:b` 表示段区间，取 `[start(a), end(b))` 的原始字节；`$`、`*` 要排除就把区间写成
  `msgid:station`（NMEA 的标准做法：XOR 从 `$` 之后到 `*` 之前）；
- `%x` / `%X` 按十六进制解析校验字节（`%o` 八进制），校验值宽度由算法决定；
- 不符 → `ChecksumMismatch`，整帧拒收（计入 `rejectedFrames`），不会当普通字段读过去。

### 10.5 段头识别

不再有 `literal`。段头用 `expect` 判等：

```json
{"name":"tag","format":"$GPGGA,","output":"none"}
```

或 `{"name":"tag","format":"%[^,]","expect":[{"op":"eq","value":"$GPGGA"}]}`。

### 10.6 已删除的词法键

| 删除 | 替代 |
|---|---|
| `literal` | `expect` 的 `eq`，或 `format` 里直接写死字面 |
| `key` | 不做 KV 形态（`a=1,b=2` 交上层拆） |
| `width` | `%5d` / `%10s` 宽度，或 `%c` + `count` |
| `rest` / `repeater` | `count:"*"` |
| `base` | `%x` / `%o` / `%b` / `%d` |
| `pattern` | `%[^,]` 或 `expect` |
| `fixed` / `signed` / `sci` | `transform`（如 `self/1000`） |
| `empty` | `on_mismatch` |

## 11. 与 v2 的差异

v2 段级 35 个键里，8 个 fixture 45 段实际只用到 13 个，其余是纸面装饰。v3 删掉的键及替代：

| 删除 | 替代 |
|---|---|
| `scale` / `offset` | `transform`（`self*0.25` 编译期折回 scale/offset） |
| `range` | `expect` 的 `[a,b)` / `>x` |
| `enum` | `expect` 的 `{1,3,5}` |
| `checks` / `digest` | 统一并入 `expect` |
| `bit`（对象） | `type:"bit"/"rbit"` + `count:"宽@偏移"` |
| `repeat` / `until` | 块的 `repeater:"*"` / 段 `count:"*"` |
| `length` | 删（长度由 `count × 类型宽` 推出） |
| `derive`（段级） | 顶层 `derived` |
| `as` | 用 `types` 别名 |
| `emit` | `output` |
| `hidden` / `plot` | `output` 三态 |
| `role` | 删 |
| `psproto` / `version` | 删（不做兼容层） |

新增：`blocks`、`derived`、`expect`（数组）、`transform`（表达式）、`output`（三态）。
