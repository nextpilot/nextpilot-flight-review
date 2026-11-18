# 解析器插件模型 v2：自描述接口 + 字符步进基类

| 项 | 值 |
|---|---|
| 状态 | 方案待开工（2026-11-18） |
| 现状 SPI | `ng/core/include/ps_parser_spi.h`（纯 C ABI）+ `ParserRegistry` |
| 目标 | 解析器继承统一接口类**自述元信息**（名称/范围/传输/参数），供打开对话框直接消费；具体解析器主要实现 `parseChar` 字符状态机，缓冲/残余/ABI 适配由基类承担 |
| 硬约束 | 解析器只认字节、不写存储、不认识宿主；headless 不链 Qt Widgets；外部插件 ABI 向后兼容 |

## 1. 现状与缺口

现有 C ABI：

- `PsParserInfo`：`id / display_name / extensions / can_sniff / streaming`；
- vtable：`create / destroy / sniff / feed(chunk) / finish / reset / info / capabilities`；
- 具体解析器（`PspxParser`）手写 vtable 闭包，自己管理跨 feed 的残余字节；`UlogStreamParser` 还是普通函数，尚未接 SPI。

缺口：

1. **自述信息不够**：没有业务领域/使用范围说明、没有支持的传输方式、没有可配置参数 schema；打开对话框无法从解析器拿到表单定义。
2. **每个解析器重复造轮子**：残余缓冲、分帧、ABI 适配、注册样板都要自己写；新格式上手成本高。
3. **`feed` 是整块喂入**：文本/流式协议（NMEA、MAVLink、自定义顺序帧）本来就是逐字符状态机，整块接口逼着实现者自己拼缓冲。

## 2. 目标（两条）

1. **继承一个接口类自述**：协议名称、使用范围（领域 + 说明）、支持的传输协议、可供配置的参数；这些信息直接喂给自定义打开对话框（见 [protocol-migration.md](protocol-migration.md) §5）。
2. **特定类主要实现 `parseChar`**：基类负责缓冲、残余字节、逐字节派发、sink 出口与 ABI 适配；子类只写字符级状态机和元信息。

## 3. 分层设计

### 3.1 描述接口 `IParserDescriptor`（headless C++）

```cpp
enum class Transport { File, Udp, Tcp, Serial /*, Can, WebSocket 预留 */ };

class IParserDescriptor
{
public:
    virtual ~IParserDescriptor() = default;

    virtual QString id() const = 0;              // 全局唯一，如 "ulog" / "psproto"
    virtual QString displayName() const = 0;     // 对话框展示名
    virtual QString description() const = 0;     // 使用范围 / 适用场景说明
    virtual FormatDomain domain() const = 0;     // 业务领域（复用 FormatDomain）
    virtual QList<Transport> transports() const = 0;
    virtual QStringList extensions() const = 0;  // 带点小写后缀，可空
    virtual bool canSniff() const = 0;

    // 可配置参数（结构与 protocol-migration §5.2 的 ParamField 同一套，提到 core 共享）
    virtual QList<ParserParam> paramSchema() const = 0;
};
```

- `ParserParam`（原 `ParamField`）上提到 core，`IFormatDriver` 与解析器描述共用一份定义，避免两套字段模型。
- **单一真源（建议）**：元信息由解析器自述；`IFormatDriver` 委托给它对应的解析器，不在驱动侧再维护一份名称/后缀/参数。
- 描述全部是纯值结构，不含 QWidget、不含解析逻辑，可 headless 单测。

### 3.2 字符步进基类 `CharStreamParser`

```cpp
class CharStreamParser : public IParserDescriptor
{
public:
    // 基类实现 SPI vtable 适配：create/destroy/feed/finish/reset/sniff
    static const PsParserVtable *parserVtable();

protected:
    // 子类主入口：逐字符状态机。满帧时经基类 helper 吐批。
    virtual void parseChar(uint8_t b, ParserSink &sink, ParseError &err) = 0;

    // 可选钩子
    virtual void onFinish(ParserSink &sink, ParseError &err) {}
    virtual int  sniffHead(const uint8_t *data, uint32_t size) { return 0; }

    // 基类提供的辅助（子类不碰裸缓冲）
    //   need(n) / take(n) / skip(n) / emitBatch(...) / residual() ...
};
```

职责划分：

| 基类 `CharStreamParser` 负责 | 子类负责 |
|---|---|
| 残余缓冲维护、chunk 追加与逐字节 drain | `parseChar` 状态机（拼帧、校验、取值） |
| SPI vtable 适配、create/destroy/reset | 元信息 getter（id/name/domain/transports/schema） |
| sink 出口、错误透传、finish 冲刷钩子 | 满帧时调 `emitBatch` 填列 |
| 内存纪律：常驻 = 残余缓冲 + 一批 | 帧内字段解释、scale/offset、字节序 |

**批量逃生门（建议保留）**：定长二进制帧格式（如 ULog D 段）逐字节开销大。基类保留一个可选 `feedChunk(data, size, sink, err)` 覆写点；不覆写就走默认逐字节路径。不为了统一而牺牲高吞吐格式的效率。

### 3.3 与现有 C ABI 的关系

- **内置 C++ 解析器**：继承 `CharStreamParser`，基类导出 vtable；`NG_REGISTER_PARSER` 宏改为接收基类子类。
- **外部插件（.so/.dll）**：保留纯 C ABI；描述信息经 vtable 尾部可选槽位返回（描述结构或 JSON）。老插件不填新槽位时，回退到旧 `PsParserInfo` 字段，不破坏兼容。

### 3.4 对话框消费

`LoadDataDialog` 枚举解析器描述：

- 按 `domain` 分组展示；
- 按 `transports` 过滤：`File` 显示数据文件行，`Udp/Tcp/Serial` 显示地址/端口行（活源第二批）；
- 按 `paramSchema()` 动态生成参数表单（控件类型/默认值/范围/选项/条件显隐，见 protocol-migration §5.3）；
- `description` 作为格式说明文字展示。

## 4. 分批与验收

| 批次 | 内容 | 验收 |
|---|---|---|
| Q1 | core 抽出 `Transport / ParserParam / IParserDescriptor`；`CharStreamParser` 基类 + vtable 适配；`PspxParser` 迁移为参考实现 | 单测：逐字节喂入拼出整帧；残余跨 feed 保留；描述字段可查；ctest 通过 |
| Q2 | `ParserRegistry` 与对话框接描述信息（领域分组/传输过滤/schema 表单）；现有解析器逐个迁到基类 | 对话框按描述生成表单；ULog/CSV 迁移后行为不变 |
| Q3 | 插件 ABI 描述槽位；活源（udp/tcp/serial）接入通路 | 外部插件可声明描述；活源能建源 |

## 5. 与协议搬迁的关系

自定义协议解析器（protocol-migration L2）直接继承 `CharStreamParser`：

- 描述：`id="psproto"`、domain Autopilot/General、transport File、`paramSchema()` 里一个 `.psproto` 模板 File 字段；
- 解码：头部段 → 选择器命中变体 → 变体段定长/变长，状态机在 `parseChar` 内推进；
- 不再自己写残余缓冲与 vtable 样板。

## 6. 待拍板

1. **描述真源**：解析器自述、驱动委托（推荐），还是驱动侧另维护一份？
2. **`parseChar` 范围**：作为默认主路径，同时保留 `feedChunk` 批量逃生门（推荐）；还是强制全部逐字节？
3. **Transport 枚举**：`File / Udp / Tcp / Serial` 是否够用，要不要现在加 `Can / WebSocket`？
4. **插件描述槽**：本批只服务内置 C++ 解析器，还是同步把描述回传槽位加进外部插件 ABI？

## 7. 红线

- 解析器不读文件、不写存储、不认识宿主；结果只经 sink 吐批。
- headless 接口不链 Qt Widgets；core 不加无关新概念。
- 内存纪律：常驻内存与文件大小无关；parseChar 内只攒当前帧 + 一批。
- 老 C ABI 向后兼容：既有插件不改编译不过。
