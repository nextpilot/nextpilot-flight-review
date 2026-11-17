# Dataset / DataSource v2 路线图

状态：M0–M5 数据/缓存主线已完成；OEM7 离线 typed loader、Live `frame_batch` 去 f64 副本、配置恢复已完成（2026-11-17）

目标：把 PlotScope 的数据层统一成“DataSource 是单个数据源实例的聚合根，Dataset 是实际数据，按数据库表模型组织”。Rust 管生产数据，C++ `MemoryDataset` 只用于测试；离线/实时共用同一套 Dataset；字段按原始类型存储；实时整帧推送；绘图通过视口 LOD 适配层取数。

## 0. 里程碑状态

- M0 typed 核心（`ps-ndf/src/typed.rs`）：完成。
- M1 typed ABI 与 `PsFrameBatch`：完成。
- M2 `DataSource`/`DataSourceSession` 聚合根、driver、配置持久化、generation：完成。
- M3 离线 loader typed 表/列（ULog/APB/CSV/NMEA/UBX/OEM7）：完成。
- M4 Canvas `dataset + table/column + 视口 LOD`：完成；主绘制/自动 Y 走 typed LOD，光标/标记仍用 ABI 提供的 f64 只读视图。
- PSC2 typed 缓存：完成；PSC1 仅保留旧通道兼容读取。
- OEM7 离线 loader：完成，复用 `Oem7MessageParser`，按日志名建 `oem7.<log>` 表，CRC 失败丢帧重搜。
- 配置恢复：启动只读 `dataSource.configs` 作为默认别名/offset，不自动连接任何源；打开文件/实时源时套用。
- Live `frame_batch`：完成，列值不再复制 f64，行数跟随内核 ring。
- M6 输入旧路径：`PsSourceSink.channel_batch`、LiveFrameSink 旧通道视图、tlog/Mavlink 逐通道采集器已删除；`PS_EXTENSION_ABI_VERSION=8`。
- `PsChannelInfo.time/value` 与 `dataset_add_channel` 仍保留给旧 Dataset 通道，供 Rust CSV 样例和缓存测试使用，下一轮再退。

## 1. 已定决策

### 1.1 时间

- 所有表统一使用 UTC 时间。
- 主键时间列用 `int64 time_us`，单位微秒。
- 原始本地时间作为普通列保存，不参与主键。
- 每表维护表内序号 `uint64 seq`，从 0 开始，用于同一微秒内多条记录排序。
- 主键：`(time_us, seq)`。
- 绘图显示时间 = UTC + `DataSource.offsetSec`；原始时间列不变。

选择 `int64` 而不是 `f64` 秒的原因：当前 epoch 下 `f64` 秒的分辨率约 0.2–0.4 µs，无法保证严格微秒整数。

### 1.2 并发（暂定，Live 阶段复核）

第一版采用单写者模型：

```text
UI 线程 30ms QTimer
  → DataSourceDriver::poll()
  → 解析器逐帧解析
  → frame_batch
  → Dataset::append_frame()
```

- 所有 Dataset 写入必须发生在 poll 线程。
- 扩展不得从任意工作线程直接回调宿主；如必须跨线程，先入队，由 poll 线程 drain。
- Dataset 不提供跨线程裸指针；Canvas 只持 `Table/Column` 引用 + `generation`。
- 删除/重载 DataSource 时先递增 generation，再释放 Dataset；Canvas 检测 generation 失效后重新绑定。

不先上全局读写锁的原因：Live LOD 需要长时间读表，读写锁会阻塞实时写入；单写者 + generation 更简单，也符合当前 poll 架构。后续如果引入后台解析线程，再补 per-table 队列或快照机制。

### 1.3 Live ring（暂定，进入 Live 阶段前重拍）

- Ring 容量按字节预算，不按固定行数。
- 每张 Table 可配置 `ring_budget_bytes`，默认 32 MiB。
- 行数 = `budget / row_bytes`；`row_bytes` 由列类型宽度 + null bitmap 计算。
- 不同表列数差异大，容量自然不同；10 列和 10000 列不会用同一个行数。

窗口语义：

- 默认跟随最新：曲线自动向左滚动。
- 用户缩放/平移后只暂停“视图跟随”，不暂停接收；数据继续写入 ring。
- 用户要停采集，走单独的暂停按钮，和视图跟随分开。
- 当前窗口完全滚出 ring 时，画布显示“已滚出缓存”，并提供“回到最新”。
- 多表不同采样率：每张表收到帧就追加；UI 重绘统一节流到 30 Hz。低速率表在下一次重绘更新，高速率表合并到同一帧重绘；所有表共用同一 UTC 时间窗口，以下一帧最大时间为跟随基准。

### 1.4 缓存

- 删除旧扁平 `channel/t/v` 缓存格式。
- 新格式 `PSC2`：Dataset → Table → Column，保存 `time_us`、`ValueType`、typed values、null bitmap、表名/列名/单位。
- 只缓存 Dense 表；Ring 表不落盘。
- 旧缓存版本直接失效，不做迁移。

### 1.5 frame_batch

- 整帧一次推送，不按字段逐个推送。
- 列顺序固定；运行期不新增列。
- `rows >= 1`，支持单帧或多帧批量。
- 每列带 `name/unit/type/values/null bitmap`。
- 回调期间指针有效，宿主负责拷贝/追加。
- 旧 `channel_batch` 在新扩展迁完后删除，不保留兼容。

### 1.6 作用域

- 全局唯一键：`(sourceKey, tableName, columnName)`。
- `sourceKey` 是持久化稳定 key（路径 / 设备身份 / 用户别名），不是运行期 `sourceId`。
- 表名在单个 DataSource 内唯一；多 parser 共享 transport 时由 parser 自己保证表名不冲突。
- 数据源 Dock 永远保留 source 层；绘图画布在单数据源时隐藏 source 层，只显示 `table.column`。

### 1.7 元数据表

- `params`、`messages`、`info` 和数值表在同一个 Dataset，但各自独立成表。
- 元数据表不参与绘图，但参与缓存、导出和 `frame_batch`。
- 列类型增加 `Text`，用于参数值、消息正文、枚举文本等。

### 1.8 非折线图

折线图先走 `PlotSeries` 窗口 LOD；散点、3D、地图、直方图、FFT 后续单独适配，不强行套用同一套 LOD。

### 1.9 导出、录制、滤波

录制和导出分开：

**录制：Raw / MCAP**

- 实时录制只保留 `Raw` 和 `MCAP`。
- `Raw`：原始协议字节 + topic + timestamp，用于回放/取证。
- `MCAP`：`frame_batch` 按表写 channel，一行一条 message，保留 typed 值和 null。
- 删除 `StreamRecorder` 的 CSV 格式；CSV 不参与录制。

**导出：CSV / MCAP**

- CSV 是给人看、给 Excel/脚本用的导出能力，不参与录制。
- CSV 以表为单位：一张表一个 CSV，行 = 表行，列 = 表列。
- 表行数不同，不能把多张表拼进同一个 CSV 的同一段行；多表导出就是多个文件或目录/压缩包。
- 文件名 `sourceKey.tableName.csv`；首列 `time_us`，可选 `time_text`；null 写空字段。
- MCAP 导出用于全保真/回放：一个 Dataset 一个 `.mcap`，每张表一个 channel，`log_time = time_us * 1000` ns，message sequence 保留 `seq`。
- 导出选中列只裁列，不改行集合；元数据表同样按表导出。

滤波器：

- 滤波器输入是某张表的一列，输出仍是同一张表的派生列，共享同一时间轴和行数。
- 输入 typed 列先转 f64 计算，输出列类型为 f64；null 行保持 null。
- 不做跨表自动对齐；跨表运算必须显式指定 join/align 规则。

### 1.10 UI 数据树

- 数据源 Dock：保留 `source → table → column` 三层，source 层不隐藏。
- 绘图画布：单数据源时隐藏 source 层，只显示 `table → column`；多数据源时用源别名区分，但不增加多余层级。
- 画布图例：单数据源用 `table.column`，多数据源用 `alias · table.column`。
- 属性面板：单数据源隐藏 source 节点；多数据源保留 source 节点。

## 2. 目标结构

```text
DataSource
├─ DataSourceConfig
├─ unique_ptr<Dataset>
└─ DataSourceDriver
   ├─ OfflineDataSourceDriver
   └─ LiveDataSourceDriver

Dataset
└─ Table
   ├─ name
   ├─ time_us: Arc<Vec<i64>>
   ├─ seq: Vec<u64>
   ├─ columns: Vec<Column>
   └─ storage: Dense | Ring

Column
├─ name
├─ unit
├─ ValueType: F64/F32/I64/U64/I32/U32/I16/U16/I8/U8/Bool/Text
├─ values: ColumnData
└─ nulls: NullBits
```

`DataSourceSession` 用 `std::deque<DataSource>` 持有实例，保证 `DataSource*` 稳定；删除 `SourceStore`。

职责边界：

- `DataSourceConfig`：模式（offline/live）、格式/扩展 id、路径、选项、实时连接参数；可持久化。
- `DataSource`：聚合根/门面，只持有 config、Dataset、driver；不直接解析、不直接开串口。
- `DataSourceDriver`：接口 `open/close/poll/reload`；Offline 负责 loader，Live 负责扩展 handle、frame sink、录制/raw tap。
- `Dataset`：唯一数据存放位置；driver 只往里写，不复制第二份。
- `PropertyProvider` / `DataSourceDock`：属性模型与 UI，不参与数据生命周期。

## 3. 接口约定

### frame_batch

```c
typedef struct PsFrameColumn {
    const char    *name;
    const char    *unit;
    PsValueType    type;
    const void    *values;
    const uint8_t *nulls;   // 1 = null，长度 (rows + 7) / 8；NULL = 全有效
} PsFrameColumn;

typedef struct PsFrameBatch {
    const char          *table;
    const int64_t       *time_us;
    const uint64_t      *seq;
    uint32_t             rows;
    uint32_t             columns;
    const PsFrameColumn *column_data;
} PsFrameBatch;
```

sink：

```c
int32_t (*frame_batch)(void *ctx, const PsFrameBatch *batch);
```

### 绘图

```cpp
class PlotSeries {
    void build(int64_t t0_us, int64_t t1_us, int px);
    const double *timeSec() const;
    const double *value() const;
    size_t count() const;
};
```

- `build` 只在窗口/像素宽度/revision 变化时重建。
- 窗口内二分 + 分桶，不扫描全量。
- 光标/命中用 `table->lowerBound()` + `column->valueAsF64(row)`。
- Canvas 不再长期持有 ring 裸指针。

## 4. 实施阶段

### M0：typed core（当前）

- `ps-ndf` 增加 typed Table/Column/Dataset 模块。
- `time_us`、`seq`、`ValueType`、`ColumnData`、`NullBits`。
- 单元测试覆盖类型、null、时间二分、窗口 LOD。
- 不改变 C++ ABI，不接入现有 loader。

验收：`cargo test -p ps-ndf` 全过；现有 ABI/构建不受影响。

### M1：ABI 与 live

- `ps-abi` 暴露 typed Table/Column 和 `frame_batch`。
- `ps-stream` 在 Live 第二阶段退役；实时 view 由 `LiveFrameSink` 自己维护。
- LiveDataSourceDriver 接收整帧；旧 `channel_batch` 保留回退，迁移完再删。
- 重建所有 builtin/dynamic 扩展。

验收：实时 MAVLink/OEM7 单帧一次回调；ring 覆盖后 UI 有明确提示；`ctest` 全过。

### M2：DataSource

- 删除 `SourceStore`；`SourceData` 改名 `DataSource`。
- `DataSourceSession` 改 `std::deque<DataSource>`。
- 引入 `DataSourceConfig`、`DataSourceDriver`、`PropertyProvider`。
- 不再保留 `LiveSession`；整帧接收下沉为 `LiveFrameSink`，由 `LiveDataSourceDriver` 持有。
- 配置持久化接 `SettingsManager`。

验收：离线/实时源都通过 `DataSource` 访问 Dataset；删除源时 Canvas 引用安全失效。

### Live 第一阶段（已完成）

- `PsFrameBatch` + `PsSourceSink::frame_batch`，追加在 sink 末尾，不破坏旧扩展 ABI。
- `LiveFrameSink` 接收整帧，`table.column` 合成通道名，null 暂写 NaN。
- `LiveDataSourceDriver` 接管 handle / sink / fanout / poll / close。
- SimSource、MAVLink（ATTITUDE/VFR_HUD/GLOBAL_POSITION_INT）、OEM7（binary/ASCII）改为整帧推送。
- 旧 `channel_batch` 保留回退。

### Live 第二阶段（进行中）

- typed `Dataset` 已有 ring：按字节预算裁剪最旧行，`seq` 裁剪后仍单调递增。
- `LiveFrameSink` 每帧先写 typed Dataset，再更新绘图 f64 视图。
- 默认每表 32 MiB；`ps_dataset_set_ring_budget` 支持 Dataset 默认预算和单表覆盖。
- `ps_dataset_lod_minmax` 已提供 typed 表的视口 LOD，绘图可直接按 `table.column` 取 `(time,min,max)`。
- `LiveSession` 已删除，`DataSource::liveSink` 是唯一实时入口。
- `ps-stream` 已删除；`LiveFrameSink` 的 view 改为按表共享时间轴，时间不再按通道复制。
- 待办：绘图改为 `Table/Column + 视口 LOD`；ring 滚出提示与跟随/暂停语义。

### M3：loaders

- ULog/APB/CSV/NMEA/UBX/OEM7 全部输出 typed Table。
- NMEA GGA/RMC/VTG 各一表；talker 作为列。
- OEM7/UBX 按报文类型建表。
- 删除旧 `PsChannelInfo.time/value` 路径。

验收：所有 fixture 解析结果与当前数值一致；表/列结构正确；`ctest` 全过。

### M4：plotting

- `PlotSeries` 窗口 LOD。
- Canvas Series 改为 `Table/Column + generation`。
- 绘图画布数据树改 Table → Column；单数据源隐藏 source 层。
- 数据源 Dock 保留 source → table → column。
- 缩放/平移/resize/revision 自动重建。
- 大范围加多级 LOD。

验收：300 万点 CSV 全范围缩放可用；光标取值不读整列；实时 ring 无全量 view 副本；单源/多源图例层级正确。

### M5：cache/export/ops

- `ps-cache` 升级 `PSC2`。
- 导出支持 CSV / MCAP：CSV 按表导出，MCAP 一个 Dataset 一个文件、每表一个 channel。
- 录制只保留 `Raw` / `MCAP`；删除 CSV 录制格式。
- `ps-ops` 输入输出 typed 适配；滤波器输出同表派生列。

### M6：删除旧路径

- 已删除 `channel_batch`、HostApi `dataset_add_channel`、旧 loader 单通道写入路径。
- CAN 原始字节按 `CAN<bus>.<id>` 一张表（`bN/period/dlc` 列），DBC 按 `CAN<bus>.<message>` 一张表，MCAP 按 topic 一张表。
- 旧扁平 loader 统一降级为“一通道一张单列表”（table=通道名、column=`value`、共享时间轴），后续再按语义拆表。
- 剩余：`PsChannelInfo.time/value` 彻底删除、PSC1 读取下线、null bitmap 全链路、OEM7 版本拆表、配置 schema 版本、实时/MCAP 导出。
- C++ `MemoryDataset` 与 Rust 跑同一组一致性用例。
- 更新 `AGENTS.md` 边界与 `docs/develop`。

## 5. 风险

- Ring 窗口语义如果不在 UI 明确提示，用户会误以为丢数据。
- typed LOD 如果每帧全表扫描，300 万点会卡；必须窗口二分 + 多级 LOD。
- 旧扩展一次性迁移量大；开发阶段可以推倒重来，但要一次改完 builtin/dynamic/tests。
- 元数据表字符串列会增加缓存/导出复杂度，先按 Text 列实现，不做富类型。
- 单写者模型依赖“扩展回调在 poll 线程”约束；新增后台线程前必须先补队列。

## 6. 后置决策（不阻塞 M0–M2）

以下细节先不定死，到对应阶段再收口：

- MCAP typed row 的具体编码（紧凑二进制 / JSON schema / FlatBuffers）与压缩策略。
- CSV 数值格式、时间格式、空值写法、超长列数处理。
- Ring 默认字节预算、每表上限、总内存预算与淘汰提示文案。
- 元数据表的具体 schema、索引和 UI 展开方式。
- 非折线图（散点/3D/地图/直方图/FFT）的取数与采样策略。
- 多 parser 共享 transport 时的表名冲突提示与命名规范。
- 滤波器对 typed 列的输出类型策略（统一 f64 还是保留原类型）。
- `SettingsManager` 的 DataSourceConfig 字段、设备身份匹配、串口枚举。
- `ps-cache` 的压缩、分块、版本迁移和损坏恢复。
- 单源/多源图例、属性面板对象树的具体视觉细节。
- Live 全链路：ring 容量与窗口语义、跟随/暂停、多速率刷新、并发模型、LiveDataSourceDriver、live 录制，统一到进入 Live 阶段前拍板；M0 只保留 `frame_batch` 和单写者约束，不提前实现 live。

原则：先定会影响数据模型、ABI、生命周期的；纯格式、纯 UI、纯性能参数后置。

