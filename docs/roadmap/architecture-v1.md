# PlotScope 架构设计与 PJ4 对标实施 · 定稿 v1.0

> 状态：历史架构决策记录，保留 D1~D16、分层、数据模型和扩展 ABI 的结论。文中目录树与里程碑是 v1.0 基线，不代表当前代码；当前架构见 [`../develop/architecture.md`](../develop/architecture.md)，硬边界见 [`../../AGENTS.md`](../../AGENTS.md)，已实现能力以 [`../manual/`](../manual/README.md) 为准。

> **一句话**：Qt 6 Widgets 壳 + Rust 数据内核（同进程 C ABI），面向飞控 / 嵌入式 / 车辆 / GNSS / 机器人的高性能绘图分析软件。
> 本文件只放架构结论与实施记录。过程稿、被否决方案不保留多份副本。
> 2026-11-13 合并：原《架构设计-定稿》《对标 PJ4 补全计划》并为一份，架构在前、实施计划为第 14 章。

---

## 0. 决策清单（只留结论）

| 编号 | 决策 | 结论 |
|---|---|---|
| D1 | 运行形态 | **Qt 6 Widgets（C++）原生桌面 + Rust 数据内核**，同进程经 C ABI（非 IPC、非 cxx、非 Web 壳） |
| D1b | 图表区渲染 | 图表用 `QWindow` + 自绘 GL/RHI 或二维码图集；**禁用 `QQuickWidget`**（每帧一次全屏纹理拷贝，4K 下 2~4ms） |
| D1c | UI 框架范围 | **主界面全 Widgets**；QML 仅允许"演示大屏/仪表页"且必须独立 `QQuickWindow` 窗口 |
| D2 | Rust 内核依赖 | `arrow-rs` + `memmap2` + `rayon` + `tokio` |
| D3 | 画布选型 | **默认自绘**：Rust LOD + `drawPolyline`，实测 7.2 ms/帧 @20ch×8k（M0-0 结论见 §12）；**JKQTPlotter 降为可选后端**（同数据 13.1 ms，仍 <16.7 ms 帧预算，接入必须由 LOD 喂数）；Qwt 仅对照（许可未核）；要抗锯齿平滑曲线则换 GPU 渲染，另立 spike |
| D3b | 画布禁用项 | Qt Charts / Qt Graphs / QCustomPlot（GPL 或商业授权）不可用 |
| D4 | 数据中间态 | **NDF**（自研列存）+ **Arrow IPC** 做缓存与交换格式 |
| D5 | 脚本 | **Lua 5.4 内嵌**（MIT，静态库），表达式与脚本同一套语法；算子数学在 Rust 向量核心里，Lua 只做编排（`userdata` + 元表，一次调用算一整块）；**不再用 QuickJS** |
| D6 | 协议模板 | YAML/JSON 帧模板 → Rust 侧热编译为状态机 |
| D7 | 停靠 | **`QDockWidget`**（Qt 内建，零第三方许可义务，四区/浮动/tabify/saveState 够用）；Qt-ADS（LGPL-2.1）作升级项，M1 交互评审不够"跟手"再换，换的成本只在 `DockHost` 一层；KDDockWidgets（商业 + GPL）排除 |
| D8 | 缺省构建 | **CMake + Corrosion**（KDAB），Rust crate 随 CMake 一起编 |
| D9 | 扩展 ABI | `ps_extension_entry()` 返回 `PsExtensionDescriptor`：编译期内嵌 manifest + 可选 `PsLoaderVtable` / `PsUiVtable`；loader 数据面 `schema_json/probe_json/default_options/params_json/load` 五函数；`abiVersion` + `ps_abi_selftest()`；四种后端 builtin/dylib/wasm/sidecar |
| D10 | 布局持久化 | `.psl`（JSON，可分享可进 git）+ `.psc-workspace`（项目级）；`semantic`/`view` 分块；幽灵曲线；键序固定、原子写；撤销走命令模式 |
| D11 | 类型系统 | f32/f64 为主 + `kEnum`（i32 码 + 字典）+ `kString`（sidecar）；布尔并入 `kEnum`(0/1) |
| D12 | 许可红线 | 主程序与内核 **BSD-3** 兼容；引入第三方必须逐条核对许可（Qwt License 1.0 待核，未核前只用 JKQTPlotter） |
| D13 | 协议编辑器定位 | **自定义 Loader 的附属编辑器**（子窗口 / `QDialog`，对应 PJ4 dialog vtable、PJ3 `optionsWidget()`）；**不设顶层标签页**，生命周期随 Loader |
| D14 | 工作区分区 | **四可停靠区（数据源/属性面板/脚本/日志）+ 固定绘图区 + 固定带**；绘图区不可停靠 |
| D15 | 页 | 一页 = 一棵 `QSplitter` 树（plot 与仪表混排），页签是页列表；每页独立时间窗，默认不联动 |
| D16 | 工程结构 | `cmake/` `src/{app,kernel,extension,function,3dparty}` `tools/` `tests/`；C++ 与 Rust 都在 `src` 下按语言分目录，`src/application/kernel` 是 Cargo workspace 根；`app` 只依赖 `ps-abi` 一个头文件 |

---

## 1. 场景与格式清单

### 1.1 六个生态的现实

| 生态 | 真实格式/协议 | 量级 | 我方进度 |
|---|---|---|---|
| PX4 | ULog（`.ulg`，自描述）+ MAVLink | 记录 50~250 Hz/主题，1 h ≈ 100~600 MB | M1 |
| ArduPilot | DataFlash（`.bin`/`.log`，FMT 自描述）+ MAVLink | 同上；大量枚举（参数/模式/故障码） | M1 |
| 嵌入式探针 | SEGGER RTT、SystemView、J-Scope、串口 ASCII/自定义二进制、DataTamer、Influx line | 10~100 kHz 单通道 | RTT M2；SystemView 后置 |
| 车辆总线 | DBC + ASC/BLF/candump + MDF4（ASAM） | 500 kbit/s~1 Mbit/s；1k~10k msg/s；**一帧数十信号** | CAN 家族 M1（待拍板）；MDF4 M2 |
| GNSS | NMEA 0183、NovAtel OEM7（ASCII+二进制）、u-blox UBX、RTCM3 | 1~50 Hz；raw 5~20 Hz | NMEA/OEM7 M2；UBX M2；RTCM 后置 |
| 机器人/自驾 | MCAP、rosbag1/2、Foxglove WS、ROS2 DDS、PCD/PLY、URDF、MP4、LeRobot | 话题 1~100 Hz；点云单帧 0.1~1 MB | MCAP/rosbag2 M3；其余后置 |

### 1.2 我们的格式矩阵（按里程碑）

| 阶段 | 内容 |
|---|---|
| **M0** | CSV/TSV |
| **M1** | ULog + **DFLog(.bin)** + **CAN 家族（DBC + ASC + BLF + candump）** + MAVLink（tlog/流） |
| **M2** | MDF4（多时间基）+ NMEA + NovAtel OEM7 + UBX + 串口/RTT 流 |
| **M3** | MCAP / rosbag2 + MP4 同步回放 |
| **后置** | Parquet、rosbag1 `.bag`、PCD/PLY、LeRobot、LSL/MQTT/ZMQ/Foxglove/Mosaico、RTCM |

### 1.3 与 PlotJuggler 4.1.0 的关系（实测结论）

- PJ4 的加载器全部移出核心，改为 **Marketplace + `pj-official-extensions`** 分发；核心只留薄壳。
- PJ4 主干：CSV / ULog / MCAP / Parquet / **MF4(+DBC)** / **BLF(+DBC)** / **candump(+DBC)** / PLY·PCD / MP4 / LeRobot；流 UDP·MQTT·ZMQ·ROS2·Foxglove·PJ-Bridge·WebRTC·LSL；解析 JSON·Protobuf·ROS1/2·Arrow·Influx·DataTamer。
- **PJ4 的四处空白，即我们的差异化**：①无 ArduPilot DFLog 加载器 ②无 GNSS 原始解析（NMEA/OEM7/UBX/RTCM）③无 Vector ASC（只有 BLF）④无 SEGGER RTT / SystemView。
- 策略：**对齐主干 + 补空白，不整抄清单。**

### 1.4 三条排序判据

1. **DBC 是元数据不是格式**：`factor`/`offset`/`unit`/位域必须映射成 L2 通道元数据变换，不能在解码后重算。
2. **先做"会崩的"，后做"能搬的"**：位域/CRC/变长帧（CAN、GNSS、DFLog）优先于纯搬运（CSV、MCAP）。
3. **能喂给协议编辑器**的格式优先（NMEA/UBX/OEM7/DBC 可直接当模板样例）。

---

## 2. 架构分层

```
┌──────────────────────── Qt 6 Widgets（C++）────────────────────────┐
│ 主窗口 / 停靠 / 通道树 / 属性面板 / hex 编辑器 / 表格 / 图表画布 / 仪表页 │
│ ExtensionCatalog / ContributionRegistry / UiDialogEngine / PanelEngine    │
└───────────────────────────────┬───────────────────────────────────┘
                      PsHostApi + 扩展 .ui（批量传递）
┌───────────────────────────────┴───────────────────────────────────┐
│                    ps-runtime（C++ 装载层）                         │
│ 扩展目录 / manifest / dlopen / ABI 校验 / 会话 / 源管理 / 写入口      │
└───────────────────────────────┬───────────────────────────────────┘
                      C ABI（只传批量数据）
┌───────────────────────────────┴───────────────────────────────────┐
│                    ps-datastore（Rust）                            │
│ NDF 列存 │ Chunk/Column │ Writer/Reader │ 查询/LOD │ 对齐/算子       │
│ 派生变换 DAG │ retention │ Arrow 缓存+mmap                          │
└───────────────────────────────────────────────────────────────────┘
```

**五层分工（硬边界）**

| 层 | 只负责 | 不允许 |
|---|---|---|
| `ps-base` | 公共类型、ID、错误码、`PsBytes`、ABI 版本 | 不碰存储、不碰 Qt |
| `ps-datastore` | 数据集、Topic/Chunk/Column、写入、查询、LOD、对齐、算子、派生 | 不认识 `.csv`/`.ulg`/OEM7 |
| `ps-runtime` | 扩展发现、manifest、`dlopen`、ABI 校验、会话、源管理、`PsHostApi` | 不画界面、不解析具体格式 |
| `ps-format` | 官方解析器：CSV/ULog/APBin/ASC/BLF/… | 不持有 UI、不建窗口 |
| `app` + `extensions` | Qt 壳、ContributionRegistry、Dialog/Panel 引擎；扩展只提供数据入口与 `.ui` | app 不 include 扩展 UI；扩展不 include app 内部 |

**归属判定三问**：① 要遍历/变换数据 → **ps-datastore**；② 决定数据从哪来 → **ps-format 或 extensions**；③ 只在屏幕上存在 → **app + 扩展 `.ui`**。

**边界四条铁律**
1. 跨 C ABI 只传批量（`{指针, 长度, 采样数, 通道数}`），绝不为单点调用。
2. UI 线程永不阻塞、永不做重活；查询异步，结果是"已降采样的只读切片"。
3. 内核不认识 `QWidget`，Qt 侧不碰 `Chunk`。
4. 曲线一律 `drawPolyline` + 关 AA + **整数 1px** 线宽：Qt 6.2 软件光栅下 AA 描边慢两个数量级（20×1500 段 367 ms vs 3.6 ms），线宽 1.3 这类小数会掉进宽线轮廓器（26.5 ms）。

**容易错判的六处归属**

| 功能 | 归属 | 判据 |
|---|---|---|
| 扩展加载、ABI 校验、`selftest` | **ps-runtime** | 管数据流不管界面 |
| 协议模板编译（YAML→状态机） | **ps-format / 扩展** | 产物是解码器，不是存储 |
| 协议编辑器本体（字段树/hex/联动/CRC 试算） | Qt（宿主提供通用控件） | 纯交互绘制；帧结构由 Loader 声明 |
| 单位换算（factor/offset） | **ps-datastore / ps-format** | 改变数值 → 影响降采样正确性 |
| 图表画布（轴/图例/游标/框选） | Qt | 数据由 Rust 给切片 |
| 仪表页（地平仪/罗盘/表盘） | Qt | `QPainter` + `QPropertyAnimation` |

**扩展 UI 归属（新增硬约束）**

- 任何格式专属的 Dialog / Ribbon / Dock / Panel 类，都放在 `extensions/<id>/`，app 不存放、不 include。
- app 只提供通用 `UiDialogEngine` / `PanelEngine`，负责加载扩展 `.ui`、绑定 `widget_data`、tick、事件、accept/reject。
- 扩展不返回 `QWidget*`，只交 `.ui` 资源 + JSON 协议；外部扩展因此不需要和宿主 Qt ABI 完全一致。
- 通用 schema 对话框是 app 的框架能力，不是某个 loader 的 UI；CSV 的 schema 由扩展声明，布局由 app 统一渲染。

---

## 3. 数据模型（NDF）

```
Chunk<8192>:
  t:      Float64Array[8192]（列内共享，所有通道共用一根时间轴）
  y[i]:   Float32Array[8192] | Int32Array[8192]（enum）| 字符串 sidecar
  gap:    1 bit / 采样（无法采信的点置位，不删、不插值）
LOD: 桶 [64, 512]，每 chunk 在 512 档为 16 桶，存 min/max/首/尾
内存: 8N + 4N·C + N/8  （N=采样数，C=通道数）
```

- **`kEnum`**：`codes: Int32Array[8192]`（与 f32 等宽，内存公式不变）；字典挂 `ChannelMeta`（`enumMap` + `enumUnknown: keep|gap`）；Arrow 用 `Dictionary<int32,string>`。
- **降采样对 enum 必须分段**：桶内多码全保留变化点，首尾补进入态；`QueryResp` 返回 `StateSegment[]`。
- 三条类型不变量：① 同一 `applicationId` 的字典不可变（改 YAML 必须重解析）② 布尔一律存 `kEnum`(0/1) ③ 字符串列不参与降采样，只返回时间区间内首末值。

---

## 4. 存储与缓存

| 项 | 结论 |
|---|---|
| 中间态 | NDF（内存列存，chunk 化） |
| 缓存文件 | **Arrow IPC File** + 分段 `mmap` + `index.json` |
| 失效判定 | 弱指纹（文件大小 + mtime + 头尾采样哈希），不作强校验 |
| 二次加载验收 | 1 GB ULog **≤400 ms**（热态 ~100 ms） |
| 超内存数据 | 全量走 mmap，内存只驻留窗口 ± 预读区 |

---

## 5. 加工层（脚本与单位）

**四层，按"是否改变数值"分层**

| 层 | 位置 | 例子 | 是否物化 |
|---|---|---|---|
| L1 显示单位 | 轴级 `displayUnit` | `m/s` → `km/h` | 否，画的时候乘 |
| L2 通道元数据变换 | 解析器读出的 `factor`/`offset`/`unit`（ULog、MDF4、DBC） | 原始计数 → 物理量 | 否，**但必须在降采样前应用** |
| L3 派生通道 | 算子（见下） | 微分/积分/滤波/四元数 | 视引用次数 |
| L4 曲线表达式 | 仅当前视图 | `ps.ch("a")*2 + ps.ch("b")` | 否 |

**可交换性判据（决定能不能"后置映射"）**

| 变换性质 | 做法 | 成本 |
|---|---|---|
| 保序仿射 / 反序仿射 / 单调非线性 | 后置映射 `MapOp`，每桶 4 点 | O(桶数) |
| 非单调（如平方） | **必须物化** | O(N) |
| 多输入（如 `a+b`） | **必须物化**，且显式对齐时间轴 | O(N) |

**铁律**：**先算派生/L2，再进 LOD 桶**。否则 min/max 丢尖峰、diff 出假导数。

**算子注册表**：一算子一 YAML（声明式 schema），宿主不实例化即读 schema 并自动生成参数面板：

```yaml
id: derivative
name: 一阶微分
output_kind: scalar
params:
  - {name: window, type: integer, default: 1, min: 1, max: 1000, unit: samples}
monotone: false      # false → 自动物化
affine: false
```

### 5.1 脚本引擎：Lua 5.4 内嵌（D5 修订）

结论：**用 Lua，不用 QuickJS，也不再自研表达式 AST 解析器**。理由不是"Lua 更高级"，是三条硬账：

| 维度 | 内嵌 Lua 5.4 | 内嵌 QuickJS | 自研表达式 DSL |
|---|---|---|---|
| 体积 | 源码约 3 万行，x64 静态库 **250~400 KB** | ~1 MB 级 | 0（但解析器 3~5k 行代码是我们自己维护） |
| 性能（紧凑数值循环） | 每秒数千万次简单算术 | 同机约为 PUC Lua 的 1/2~1/3（M0 复测口径） | 取决于我们写成什么样 |
| 语法学习成本 | 1 小时能写对 | JS 熟手更快 | 用户得学"我们家的方言" |
| 内嵌难度 | 一个 `luaL_newstate` + 白名单库；**无异常、无 C++ ABI** | 要处理 JS 值栈与 GC 句柄 | 要写词法/语法/类型检查/报错定位 |
| 沙箱 | 不开 `io/os/package/load` 即基本封闭；`lua_sethook` 做指令预算，自定义 allocator 做内存上限 | 需自己裁 std | 天然封闭 |
| 许可 | MIT（与 BSD-3 兼容） | MIT | 自家 |

**关键设计：Lua 不碰逐样本循环。** 通道以 `userdata` 交给 Lua，算术走元表，一次运算符调用处理**一整块数组**（攒批的 10 ms 块），真正的加减乘除、滤波、四元数在 Rust 里跑（`rayon` 分片、连续内存、无分支热循环）。Lua 的职责只是"把算子图拼起来 + 分支判断 + 多输出命名"：

```lua
local ps = require 'ps'
local ax, ay = ps.ch('imu.acc_x'), ps.ch('imu.acc_y')
out = ps.sqrt(ax*ax + ay*ay)      -- 3 次 C 调用，每次算整块
out.name, out.unit = 'acc_norm', 'm/s^2'
```

这条路径的量化口径：每脚本每块一次 Lua 调用，200 条脚本 × 100 块/秒 = **2 万次调用/秒**，调用开销完全可忽略；真正的成本在内存带宽（向量算子基本是 memory-bound，2~5 GB/s 级）。

**所以标量循环是"能用但别用"**：写成 `for i=1,n do ... end` 逐样本跑，速度掉到 1/100，等于把实时性赌在解释器上。宿主在日志里给一次显式告警（同一条脚本只警告一次），并把它标成"慢脚本"。

**表达式栏就是 Lua 语法糖**：用户输入 `a*2+b`，宿主包成 `return ps.ch('a')*2 + ps.ch('b')` 再编译。这样"轻量表达式"与"完整脚本"共用一套语法、一个编译器、一份错误定位，省掉整个 DSL。

**沙箱与失败语义（一条都不能省）**：

| 项 | 做法 |
|---|---|
| 库白名单 | 只开 `base`（去掉 `dofile/loadfile/load/require`）、`math`、`string`、`table`；`os`、`io`、`package`、`debug` 全不开 |
| 指令预算 | `lua_sethook` 每 10k 指令查一次墙钟，单块超 2 ms 即中止该脚本 |
| 内存上限 | 自定义 allocator，单脚本 64 MB，超限返回 `LUA_ERRMEM` |
| 不确定性 | 禁 `os.time/os.clock/math.random`（随机需显式传种子），保证同一份数据两次跑出同一条曲线 |
| 出错隔离 | 每脚本独立 `lua_State` + `pcall`；出错只把该派生通道标 `kError`（曲线变灰），不拖停整条流水线 |
| 热重载 | 脚本文本 hash 变化才重编译；编译失败保留上一版继续跑，日志给行号；重载只影响后续块，已物化的历史需显式"重算" |

**为什么不上 LuaJIT**：LuaJIT 数值循环快 5~20 倍，但 2.1 长期停在 beta、x64 之外的平台支持弱、且与我们"避免在边界算术上赌第三方"的原则冲突。留作 M2 的 spike：**只有当"向量算子路径实测不达标"时才考虑**，而不是默认。

---

## 6. 绘图

| 项 | 结论 |
|---|---|
| 库 | **JKQTPlotter**（LGPL 2.1、官方 CMake、Qt6 实测至 6.10、有 secondary axis + LaTeX）；Qwt 仅作 spike 对照（6.3.0 / 2024-05，无官方 CMake，Qwt License 1.0 待核） |
| 降采样 | **min/max 分桶**（可视窗口 + 像素宽度定桶数），非平均 |
| 交互清单 | 框选缩放、滚轮缩放、拖拽平移、游标（多轴同步）、图例带实时值、暂停后回溯、自适应 Y 轴、缓存上限 |
| 自绘起点 | M2 只自绘"实时滚动面板"（GPU 顶点批量上传）；M3 视瓶颈扩展到全量 |
| 工时账单（自绘） | 几何 1~2 周 / 文本 2~3 周 / 交互与算法 3~5 周（这是优先用现成库的理由） |

---

## 7. 仪表页

- `QPainter` 自绘：地平仪、罗盘、表盘、条状指示、告警灯；动效 `QPropertyAnimation`。
- 数值不跳动靠**定点格式化 + 固定宽度布局**。
- 若将来要触屏/大屏演示且 Widgets 效果不达标 → 独立 `QQuickWindow` 接 QML，主界面不动。**不许 `QQuickWidget`。**

---

## 8. 工作区：区域划分与停靠

**四个可停靠区 + 一条固定带 + 一个浮动池。绘图区永不停靠。**

| 区 | 内容 | 可停靠 | 可多实例 |
|---|---|---|---|
| **A 数据源** | 并列多个源（`ulog://`、`can://`、`mavlink://`），每源下三个视图：**数据字段 / 参数 param / 消息 message** | 左、右、底、浮动 | 否（一实例，内部并列多源） |
| **B 绘图区** | 页（Page）容器：一页 = 一棵切分树，叶子是 plot 或仪表 | **否**（主体固定居中，吃掉剩余空间） | 否 |
| **C 属性面板** | 同一个面板，按**点中的对象**换内容：曲线 / 坐标轴 / 画布 / 仪表 / 页 / 参数 / 消息 / 数据源 | 左、右、底、浮动 | 否 |
| **D 脚本** | Lua 脚本行 + 派生通道列表 + 执行日志 | 左、右、底、浮动 | 是（第二实例可只留表达式） |
| **E 日志诊断** | 加载日志、扩展告警、丢弃计数、帧时间 | 左、右、底、浮动 | 是 |
| **F 固定带** | 顶部：**Ribbon**（六个任务页签 + 传输控制）+ 页签栏；底部：状态栏（吞吐/延迟/内存/22 ms 预算条） | 否 | — |

### 8.1 为什么绘图区不可停靠

画布是主体而不是工具窗。一旦允许把它拖走，立刻出现"中间空了只剩一排工具窗"的状态，而画布的价值就在于**吃掉全部剩余空间**。PJ4 同样把中间画布区固定。可停靠的只有"辅助信息"。

### 8.2 页（Page）的语义

- 一页 = 一棵 **`QSplitter` 树**，叶子两类：`PlotCanvas`、`InstrumentWidget`，可以同页混排（2 plot + 1 地平仪）。
- 页签（`QTabBar`）就是页的列表：`姿态与振动` `车辆总线` `混排` `仪表大屏` `+`。
- **页签栏只跨绘图区宽度，不横跨窗口**：页属于绘图区，不属于窗口；宽度跟着绘图列走（左/右停靠区怎么拖，它就在中间那段里伸缩）。页签条挂在 `PlotArea` 顶部，不放在 `QMainWindow` 的菜单区——否则左右停靠区会显得被页签"压住"，视觉上关系就错了。
- **"多 tab" 指的是页，不是多窗口**；一页之内竖切 3 个 plot，用 `QSplitter` 的可拖分隔条，比例入库。
- 每页有自己的时间窗（默认继承主源视窗），跨页联动是可选开关，默认不联动（否则 4 页一起重绘，白送 4× 绘制成本）。

### 8.3 两层布局持久化（与 D10 对齐）

| 层 | 存的什么 | 关键字段 |
|---|---|---|
| 工作区层 | 哪些 dock 在左/右/底、浮动位置与尺寸、可见性、分隔条比例 | `workspace.docks[]` |
| 页层 | 每页切分树、叶子类型、绑定的 `源:通道`、坐标轴、单位、LOD、仪表参数 | `pages[].splitTree` |

两份进同一个 `.psl`：`semantic{}`（通道绑定、单位、轴、页结构）必须保留；`view{}`（像素尺寸、停靠位置）可丢 —— 换机器/换分辨率时 width 全错。恢复流程：先按 `semantic` 重建，再按 `view` 尽力贴合，贴不上就退回默认。

**未连接的源不静默消失**：数据源区保留条目并标"未连接"，页里引用它的通道退化为**幽灵曲线**（空壳 + 标记），重新连接同一 URI 即恢复。

### 8.4 数据源区为什么按源分视图

三个源就是三套命名空间：`ulog://uav.attitude/roll`、`can://can0/ENGINE_STATUS/engine_rpm`、`mavlink://1/ATTITUDE/roll`。平铺必然重名。所以：

- 列表项一律显示 `源 › 命名空间 › 字段`，拖到画布时把源前缀一起带过去（绑定键含源，不含源就是错绑）。
- **参数（param）只读快照**：来自 ULog/DFLog 的参数表、DBC 的 `BA_` 属性、MAVLink `PARAM_VALUE`。用途是"这条曲线当时的增益是多少"的对照，不做写入。
- **消息（message）是结构视图**：`vehicle_attitude` 这类主题/报文的字段清单与频率，点一条即展开成曲线组。
- 源的行内状态：`已连接 / 只读回放 / 未连接 / 校验失败`。只有"已连接"能新增绑定。
- **在线源必须带开始/暂停**（文件源不带）：每行一个 17px 的 ▶/⏸ 按钮 + 状态字 + 速率；区头有"全部暂停/全部开始"；状态栏显示 `N 源采集中 · M 暂停`。

**在线源的三个状态与语义**（不能混）：

| 状态 | 含义 | 数据行为 |
|---|---|---|
| 未连接 | 源没开，只有配置 | 不取数，树里全是幽灵 |
| 运行中 | 正在取数 | 进环形缓冲 → 每 10 ms 攒批 → 引擎入列存 |
| 已暂停 | 停止取数，**不清缓冲** | 保留最后 5 s；恢复时在时间线上插一个**缺口标**（gap），不是假装连续 |

暂停不能实现成"停止消费"：那会让上游积压、内存涨到被 OOM 杀掉。正确做法是**背压丢旧留新**（环形缓冲满就覆盖最旧），并累计"丢帧数"显示在状态栏——用户看曲线断没断，先看这个数。

### 8.5 多源同时接入的唯一耦合点

时间轴以**主源**为基准（可切换），其它源按时间戳对齐：`就近 / 线性插值 / 零阶保持` 三选一，在各自的 Loader 编辑器里配（见 §9）。除此之外各源互不干涉：独立解析线程、独立列存、独立 LOD。

### 8.6 顶部为什么用 Ribbon

PJ 现在的界面（实测）：`File` 下拉 + 左侧竖排六个分组（Data / Layout / Streaming / Publishers / Timeseries List / Custom Series），命令和状态混在一列，一屏只露 3~5 个入口，想"画一条曲线"得先去 Timeseries List 里翻。这是 2005 年的 Win32 工具箱审美：3D 边框、灰底、缩进分隔。

Ribbon 的价值不是好看，是**把命令按任务分组、一屏全露**：组名就是任务名（数据源 / 缓存 / 源管理），一屏 12~16 个高频命令，点一次就到。

**库选型（spike 实测 2026-11-12）**：**SARibbon 2.9.6**（MIT；CMake 包 `SARibbonBar::SARibbonBar`；Qt 6.2.4 编译通过；静态库单 target；源码 24 MB）。七页签 + panel + 右侧 `rightButtonGroup()` 全成立；`CompactTwoRow` + 隐藏标题栏 + tab 26 px / panel 标题 15 px / 图标 32 px → **展开 97 px**，`setMinimumMode(true)` → **折叠 28 px**（原型目标 74 / 32 px，展开态高 23 px、折叠态还低 4 px）。两个坑：静态库必须手动 `Q_INIT_RESOURCE(SARibbonResource)`，否则 dark 主题静默回退浅色 QSS；内置观感是 Office 风格，要还原原型那套扁平/1px 分隔必须整体重写 QSS 并换自绘 SVG 图标。

| 项 | 结论 |
|---|---|
| 页签 | **文件** · 数据源 · 绘图 · 仪表 · 脚本 · 协议 · 视图（文件页专管布局/设置/截图/导出，不与数据操作混） |
| 组 | 每组 ≤ 3 个大按钮 + ≤ 2 列小按钮；组名是任务，不是名词堆 |
| 尺寸 | 展开 = 32 px 页签行 + 74 px 组区；折叠 = 32 px；双击页签折叠（Office 行为） |
| 实现 | `QTabWidget` 做页签 + `QToolButton`（`ToolButtonStyle=TextUnderIcon`，54×54）做按钮 + `QLabel` 做组名。**不用 `QToolBar` 堆叠**，那会退回 PJ 的工具栏 |
| 命令源 | Ribbon、右键菜单、快捷键共用**同一份 `QAction` 注册表**：一处禁用三处同步，避免"按钮亮着但命令不可用" |
| 图标 | 单色线条 SVG，20 px / 1.4 px 描边，一个视觉族；不混彩色图标 |
| 代价 | 吃 74 px 垂直空间 → 1080p 笔记本必须能折叠；窗口 <1100 px 自动折叠并隐藏度量芯片 |
| 视觉 | 平涂、1 px 发丝线、无 3D 边框、无渐变；强调色只有一个（琥珀），只标"当前页签 / 当前选中" |

> 定稿立场：Ribbon 只承载**高频命令**。低频配置（Loader 选项、协议模板、对齐策略）仍然走各自对话框（§9），不往 Ribbon 里塞第二排按钮。

### 8.7 属性面板：按"点中的对象"换内容

不叫"检查器"——它检查的不是程序状态，而是**你刚点中的那个对象**。一个面板，八种内容，切换的判据只有"选中了什么"。

| 点中 | 面板显示 | 可改 |
|---|---|---|
| 曲线 | 绑定键 `源›命名空间›字段`、类型（kFloat32/kEnum/kString）、采样率、L1 显示单位、颜色、线宽、所属轴、是否幽灵 | 单位、颜色、线宽、所属轴 |
| 坐标轴 | 所属画布、Y 范围、自动缩放开关、刻度数、单位、是否对数 | Y 范围、自动缩放、刻度数、对数 |
| 画布 | 所属页、切分位置（比例）、曲线数、网格、游标联动、Y 自适应 | 网格、联动、自适应 |
| 仪表 | 控件类型、绑定通道、刷新率、量程、合成耗时、是否 QML | 量程、刷新率、绑定 |
| 页 | 页名、切分树摘要、时间窗（独立/跟随主源）、跨页联动 | 页名、时间窗策略 |
| 参数 | 名字、值+单位、来源、是否可写（恒为否）、用途 | 无（只读快照） |
| 消息 | 消息名、频率、字段清单、来源 | 无（结构视图） |
| 数据源 | URI、Loader 类型与 ABI、运行状态（未连接/运行中/已暂停）、速率、背压与丢帧数、通道数、对齐策略 | **开始/暂停**、断开（暂停并保留缓冲）、设为主源、打开编辑器 |

**命中优先级**（一张画布上三种对象叠着，必须定死顺序）：

1. 左侧刻度带（画布 `x < 42px` 且落在绘图区高度内）→ **坐标轴**；
2. 距任一可见曲线 ≤ 14 px → **曲线**（多条命中取最近）；
3. 其余绘图区 → **画布**；
4. 画布外的页签 → **页**；仪表格子 → **仪表**。

**为什么不做右键菜单式属性**：右键菜单要记"我在哪个对象上右键"，还要处理菜单残留；左键直接选中 + 常驻面板，选中态在画布上要有可见回执（曲线加粗、轴带淡琥珀底、画布描边），否则用户不知道面板在说谁。选中态与 `.psl` 的 `semantic` 一起存（下次打开还原到同一对象）。

### 8.8 拖拽的最小实现集（M0 必做）

1. 拖 dock 标题栏 → 左/右/底三个边缘带高亮（各占 22%），松手即停靠；
2. 拖出边缘 → 变浮动窗（`Qt::Tool`），标题栏可再拖回；
3. 拖分隔条改比例，双击复位默认；
4. tabify（两个 dock 叠成一组 tab）**推迟到 M1**，M0 只做四向停靠 + 浮动，避免第一版就陷进 dock 框架的深水区。

默认尺寸：左 216、右 240、底 132，画布最小 640×360。

---

### 8.9 画布上的五个基本操作

| 操作 | 交互 | 结果 |
|---|---|---|
| 建曲线 | 从数据源树把字段**拖到画布**（落到轴刻度带也一样） | 进本画布的轴，绑定键带源前缀；同字段重复拖不重复建 |
| 选中 | 左键点曲线 / 轴 / 画布 | 属性面板换内容（§8.7） |
| 缩放 | 滚轮：指针在绘图区缩 X、在左侧刻度带缩 Y，锚在指针位置 | 只改视窗与量程，不动数据 |
| 平移 | 拖动：绘图区平移 X、刻度带平移 Y | 拖动阈值 3 px，与点选互不误触 |
| 复位 | 双击画布 | X 回全时长，Y 回原始量程（`BASE` 快照） |

**右键三套菜单，作用域各自独立**：

- **曲线**：属性、隐藏/显示、换色、仅按这条曲线自适应 Y、新建同单位轴（M1）、导出该通道 CSV
- **坐标轴**：Y 自适应、固定当前范围、重置原始量程、对数轴、Y 反向、复制范围到其它轴
- **画布**：Y 自适应、重置时间窗、隐藏全部曲线、导出本画布 PNG

> 为什么不全塞进属性面板：面板管"这个对象现在是什么"，菜单管"以这个对象为作用域做一次动作"。混在一起，面板就退化成命令堆。

**截图与图片另存为**：当前页所有画布合成一张 PNG（带页名与时间窗），不是 OS 抓屏——按 DPI 放大绘制，导出 2× 位图；仪表页由 QPainter 直接渲染进 `QImage`，不依赖窗口可见。

**布局与设置分开导出**：`.psl` 存布局语义（页/切分树/轴范围/绑定/停靠/选中对象），`.psc-settings` 存全局偏好（主题/单位/缓冲/内存上限/线程数/丢帧策略）。都是 JSON，可进 git diff；导入只回填轴范围、绑定显隐、视窗与选中态，不重建数据。

## 9. 协议编辑器（自定义 Loader 的附属编辑器，不是独立模块）

**定位**：它不是第六个模块，而是 `data_loader` 这类 Loader 的"我是怎么解帧的"那份 UI —— 等价于 PJ4 的 **dialog vtable**（`PJ_get_dialog_vtable`）、PJ3 的 `virtual QWidget* optionsWidget()`。宿主给容器和通用控件，帧结构由 Loader 自己声明。

| 项 | 结论 |
|---|---|
| 归属 | 属 Loader 扩展，随 Loader 注册/销毁；**不做顶层标签页** |
| 入口 | ①加载向导里"新建/编辑协议" ②数据源列表每行的 ⚙ ③菜单 加载 → 编辑协议 |
| 形态 | `QDialog` + `Qt::Tool` 子窗口：独立标题栏与关闭，不参与主工作区布局持久化 |
| 宿主提供 | 字段树 / hex 视图 / 三向联动 / 位域标尺 / CRC 试算 / 模板编译（Rust 热编译）这套通用控件 |
| Loader 提供 | 同步字、长度规则、ID、字段位域、CRC 参数、候选样例帧、字段名与单位 |
| 无专用 Loader 时 | 内置"通用帧模板"：任意变长帧 + 位域，模板存为 `.psproto` 可复用 |
| 位域 | 起始位 + 位宽 + 有/无符号 + 字节序 + 跨字节；CRC 自动试算 |
| 模板格式 | YAML/JSON → Rust 状态机（编辑不重启，失败回滚上一版） |
| 验收素材 | NMEA / UBX / NovAtel OEM7 / DBC 四套真实帧 |
| 超越点 | JCom 只做**固定帧长 + 字节级**；我们做**变长帧 + 位域** |

**为什么不做标签页**：tab 是给"长期并行观看的对象"（曲线、仪表）用的；协议编辑是**低频、有明确进出**的配置动作，且必须绑定到某一个 Loader。放进 tab 会出现"没有 Loader 的空编辑器"和 tab `/` Loader 生命周期不同步两个烂摊子。子窗口天然绑定调用方：Loader 关掉，编辑器跟着关。

---

## 10. 扩展与 ABI

**扩展不是“loader 的子类型”，而是一组 family：DataSource / Parser / Toolbox / Panel。** 每个扩展包自带 `manifest.json`、数据 vtable、可选 UI vtable 和 `.ui` 资源；`ps-runtime` 负责发现和加载，app 只负责渲染。

```text
extensions/<id>/
├── manifest.json
├── lib<id>.so
├── <id>_open.ui          # 可选：扩展自己的对话框
├── <id>_status.ui        # 可选：长生命周期面板
└── ribbon.json           # 可选：声明式命令
```

**当前落地（M1 第一步）**

- `src/include/ps_extension_api.h`：`PsExtensionDescriptor` + `PsLoaderVtable` + `PsUiVtable`，声明 `family / capabilities / extensions`，纯 C、零 Qt 依赖。
- `src/extensions/dynamic/data_loader/{csv,ulog,apbin,asc,blf}/`：目录、target、入口、manifest id 统一为 `data_loader_<id>`；schema/probe/default/params/load 全在扩展内，app 不再有格式专属代码。
- `src/runtime/ExtensionRuntime.cpp`：递归扫描 `*.so`，子进程预检隔离坏扩展，`dlopen` + `ps_extension_entry` + ABI 校验；重复 id 只保留第一个，坏扩展只记错误，不拖死宿主。
- `src/application/shell/UiDialogEngine.cpp`：通用 `QUiLoader` 引擎，按扩展 `uiDialog` 加载 `.ui`，取值走 `uiData` JSON；app 不 include 任何扩展 UI 头文件。
- `LoaderRegistry::registerDescriptor()` / `scanExtensions()`：app 唯一注册入口，只收 `PS_FAMILY_DATA_LOADER`；扩展名由扩展 `extensions` 声明，不再写 app 分支。
- `src/datastore/DatasetCache.cpp`：解析结果磁盘缓存，键为源文件 + `loader@version` + 选项；校验 size/mtime，命中直接 `ps_dataset_new/add_channel` 还原，不重新 probe/parse。
- `tests/unit/test_extension_csv.cpp` / `test_extension_file_formats.cpp` / `test_extension_runtime.cpp` / `test_dataset_cache.cpp`：覆盖内置扩展、外部 `.so`、非法 ABI、重复 id 与缓存失效。

**C ABI（数据面 8 函数；扩展面 5 函数）**

```c
uint32_t           ps_abi_version(void);
const PsExtensionInfo* ps_extension_info(void);            // 清单（编译期内嵌）
int32_t            ps_probe(const uint8_t* head, size_t n);
int32_t            ps_open(const char* cfg_json, PsSink sink);
int32_t            ps_decode(PsCtx* ctx, const uint8_t* buf, size_t n, PsBatchOut* out);
void               ps_close(PsCtx* ctx);
int32_t            ps_abi_selftest(void);
```

- `PsBatchOut`：**host 预分配**，扩展零拷贝写回；失败时**扩展保留所有权、host 不释放**（成功才转移）。
- 结构体带 `abiVersion` + `reserved[8]`，为后续扩展留位。

**三种写入粒度（照 PJ4，缺一不可）**

| 粒度 | 用途 |
|---|---|
| `append_record`（按名） | 通用、低频 |
| `append_bound_record`（预绑定 handle） | 热路径，免查表 |
| **`append_arrow_stream`** | 列式批量（CSV/Parquet/ULog），零拷贝整批进 |

**封装块归宿主**：扩展只 push 行，宿主 `flushPending` 时按 `Chunk<8192>` 封块提交。

**宿主面已实现（M1，`ps_kernel.h` 只留通用 API）**：`ps_dataset_new / add_channel / channel_count / channel / free`、`ps_preview_filter / apply_filter / apply_quat_rpy / remove_channel`、`ps_lod_minmax`。格式解析与 probe 全部下沉到扩展，内核不再认识 CSV/ULog/APBin/ASC/BLF。

**四种后端**

| 后端 | 吞吐 | 何时用 |
|---|---|---|
| builtin | 200~500 MB/s | 我们自己的解析器 |
| dylib（in-proc） | 200~500 MB/s | 成熟第三方 C/C++/Rust |
| sidecar（进程外） | 100~300 MB/s | Python/Node/dotnet **私有格式**（崩了不拖主程序） |
| wasm | 中 | 不可信第三方，需沙箱 |

**准入与分发（三条硬要求）**

1. `extension check` **必须在子进程里 `dlopen`**——坏 `.so` 一加载就 `abort`，否则主程序/CI 被拖死。
2. 扩展清单**编译期内嵌进二进制**，不用外部 JSON（杜绝不同步、被手改）。
3. 闸门分两级：**`abiVersion` 挡不兼容**、**`minSdkVersion` 挡"缺能力"**；宿主版本号不参与 gate。

**双引擎 lockstep**：流式暂停时写入切副引擎、恢复切回；`ensure_topic/field` 两侧用**同一 id** 重放，保证扩展缓存的 handle 切换后仍有效（对应"冻结视图 ≠ 停止采集"）。

**坏值策略**：解码层严格（扩展解码错必须定位）；存储层宽松（装不进列类型 → 存 null + 每字段每 host 告警一次，不整份报废）。

---

## 11. 性能预算（同进程，端到端 ≤22 ms）

| 阶段 | 预算 |
|---|---|
| 收包 → 解码 | ≤0.5 ms |
| 攒批写入 host 缓冲 | ≤0.3 ms |
| GUI 取 batch | ≤0.2 ms |
| 查询 + LOD | ≤3 ms |
| 顶点上传 | ≤1 ms |
| 绘制 | ≤16.7 ms |
| **合计** | **≤22 ms** |

四条铁律：批量跨 ABI；UI 线程不阻塞；降采样在 Rust 侧完成；曲线 1px 关 AA。

---

## 12. 工程结构与里程碑

```
plotscope/
├── CMakeLists.txt                 # 只做三件事：设选项、加子目录、install
├── docs/                          # ★ 设计文档与可评审原型（随仓走，不进安装包）
│   ├── architecture.md
│   └── prototype/PlotScope-工作区原型.html
├── cmake/                         # 自写 CMake 模块（不用宏库）
│   ├── Rust.cmake                 # 内核导入：优先 Corrosion，离线退化 cargo 直调（接口一致）
│   ├── FindJKQTPlotter.cmake      # 上游没有官方 CMake 包，得自己找
│   ├── Warnings.cmake             # /W4 + -Wall -Wextra，内核头文件降级为 system
│   ├── Version.cmake              # 版本号单一来源，生成 version.h
│   └── Install.cmake              # 安装布局：bin / extensions / function / share
│
├── src/
│   ├── kernel/                    # ★ Rust 内核源码：Cargo workspace 根（不是编译产物）
│   │   ├── Cargo.toml             # workspace + members
│   │   ├── ps-abi/                # D9 的 5 个函数 + PsBytes/PsBatchOut + abiVersion + selftest
│   │   │   └── include/ps_kernel.h # 唯一对 C++ 暴露的头文件
│   │   ├── ps-ndf/                # NDF 列存、Arrow IPC、mmap、LOD 桶
│   │   ├── ps-ops/                # 向量算子（add/mul/filter/quat…）+ Lua userdata 元表
│   │   ├── ps-script/             # Lua 5.4 宿主：沙箱、指令预算、内存上限、热重载
│   │   ├── ps-align/              # 多源时间对齐（就近/线性/零阶保持）
│   │   └── target/                # cargo 产物目录（.gitignore）
│   │
│   ├── app/                       # ★ C++/Qt6 Widgets 应用
│   │   ├── main.cpp
│   │   ├── shell/                 # Ribbon、DockHost、Workspace、页签栏
│   │   ├── pages/                 # Page = QSplitter 树，叶子装配
│   │   ├── canvas/                # Canvas、Axis、CurveLayer、游标/框选/LOD 调度
│   │   ├── inst/                  # 地平仪、罗盘、表盘、告警灯（QPainter 自绘）
│   │   ├── panels/                # 数据源树、属性面板（QObject 选中 → 属性模型）
│   │   ├── proto/                 # 协议编辑器：Loader 的 options 子窗口（帧模板/位域/CRC）
│   │   └── bridge/                # 只调 ps-abi 的封装层 + QObject 信号适配
│   ├── extension/                    # ★ 官方 Loader 扩展（C ABI 动态库，产品代码而非示例）
│   │   ├── builtin_ulog/          # 每个家族一个目录：源码 + manifest.json + 测试样例
│   │   └── …
│   ├── function/                  # ★ Lua 脚本与算子 schema（随安装包分发）
│   │   ├── ops/*.lua + *.yaml     # 一算子一脚本 + 一份 schema（宿主据此生成参数面板）
│   │   ├── batch/*.lua            # 批处理：重采样、导出、批量单位转换
│   │   └── lib/ps.lua             # 对 ps.ch() 等宿主 API 的薄封装
│   └── 3dparty/                   # ★ 只收"必须锁版本 / 必须打补丁"的源码
│       ├── jkqtplotter/           # 画布首选（git submodule + 我们的小 patch）
│       └── lua-5.4/               # 脚本引擎（保持原样，不打补丁）
│                                  # 其余依赖走 FetchContent，不进仓
│
├── tools/                          # ★ 开发/发布工具，全部命令行
│   ├── ps-extension-check/           # Qt-free 子进程：dlopen 准入检查（照 PJ4 的思路）
│   ├── ps-bench/                  # 帧时间/吞吐基线，输出机器可读结果
│   ├── ps-pack/                   # 打包：windeployqt + 签名 + 生成 checksum
│   └── ps-icon/                   # 资源与图标生成（SVG → ico/png 多尺寸）
│
└── tests/                          # ★ 五类，别混在 src 里
    ├── unit/                      # C++ 单测（Qt Test）：拆分树、属性模型、页签、撤销
    ├── kernel/                    # cargo test：解析器对拍、算子数值、对齐边界
    ├── golden/                    # 样例 → 期望产物（哈希清单 + 小样本，二进制库单独放）
    ├── perf/                      # 性能回归：22 ms 预算、10M 点加载、20 通道 60 fps
    └── fixture/                   # 小体积测试日志（每个 ≤ 2 MB，不含版权数据）
```

**目录规则（不是审美，是边界）**

| 规则 | 后果如果不遵守 |
|---|---|
| C++ 与 Rust 都放 `src/`，**按语言分目录**（`app`/`extension` vs `kernel`），不设两个顶层代码根 | 两套"代码根"让新人分不清产品入口在哪；`src/application/kernel/` 当 Cargo workspace 根完全合法，代价只是 `target/` 落在 `src/application/kernel/target`、CI 缓存路径跟着改 |
| `src/application` 只允许 `#include "ps_kernel.h"`，不得 include 内核内部头 | 一旦 C++ 直接依赖 Rust 内部类型，C ABI 就白设计了，未来换内核实现等于重写 UI |
| `src/extensions/dynamic` 是**官方扩展**（随包发），第三方扩展源码不进仓 | 官方与第三方混住，会让"扩展准入检查"这条安全边界失效 |
| `src/3dparty` 只放锁版本/打补丁的库 | 什么都往里塞，仓库三年后 2 GB，clone 一次十分钟 |
| `src/function` 只放 `.lua` 与算子 schema，不放 C++ | Lua 脚本要能热重载、能被用户改，和二进制解耦才能做到 |
| `tests/` 五类分目录，`golden` 用哈希清单 | 二进制样例堆进 git，仓库体积按周膨胀 |
| `docs/` 放设计文档与单文件原型，与控制台代码同仓 | 文档离代码越远越快过期；单文件 HTML 保证 clone 下来双击就能评审，不依赖本地服务 |

**CMake 目标（4 个，不多）**

```cmake
plotscope_app        # 可执行：main.cpp + src/application/**（瘦壳，不含业务实现）
plotscope_core       # 静态库：pages/plot/inst/panels/proto，app 链接它
ps_kernel            # Rust staticlib 目标（src/application/kernel/ps-abi，Corrosion 或 cargo 导入）
ps_extension_<family>_<id> # 扩展库：src/extensions/dynamic/<family>/<id>/，输出到 bin/extensions
```

选项：`PS_BUILD_TESTS`（默认 ON）、`PS_ENABLE_LUA`（默认 ON，关掉则只留内置算子）、`PS_USE_QWT_FALLBACK`（M0-0 对照组）、`PS_EXTENSION_DIR`（默认 `bin/extensions`）。

**M0 已落地（2026-11-12，仓库 main `fceab7b`）**

- `cmake/Rust.cmake` 改为 cargo 直调：离线机器无网，Corrosion 的 FetchContent 拉不下来；有网时换回 Corrosion 只改这一个文件，对外接口（`ps_kernel` 目标 + `ps_kernel.h`）不变。
- 内核 crate：`ps-abi`（`ps_abi_version` / `ps_kernel_version` / `ps_abi_selftest` + 通用 dataset）、`ps-ndf`（序列 + min/max 抽稀）、`ps-ops`（scale / add / moving_avg / filter / quat）、`ps-align`（最近邻 / 线性）、`ps-script`（Lua 预算占位）；`cargo test` 全绿。
- 主程序：Ribbon 七页签 + 四可停靠区（数据源 / 属性 / 脚本 / 日志）+ 页签式绘图区；自绘画布支持滚轮缩 Y（锚指针）、拖动平移 Y、双击复位、十字光标；状态栏直读 `ps_abi_version()` 与自检结果，证明 C ABI 已打通。
- 数据面：`ps_dataset_new / add_channel / channel_count / channel / free` / `ps_lod_minmax`；`PsChannelInfo` 的 `time/value` 指针直指内核内存（UI 零拷贝，数据集释放后失效）。CSV 的探测/解析已移到 `extensions/data_loader/csv`，支持 `, ; \t` 与 ns/us/ms/s。
- 降采样落到 Rust：UI 每次重绘按像素宽度调 `ps_lod_minmax`，只拿 ≤px 个 `(x,min,max)` 三元组；3000 点 CSV 画到三格（每格一个通道，避免混量纲压成直线）。
- 内核单测 19 例（`cargo test`），`ctest` 2/2 通过（ABI 冒烟 + cargo）。
- **ULog 已接**（2026-11-12 晚，现为扩展侧 `UlogParser`）：按 PX4 `ulog_file_format` 实现（16 字节头 + 消息流，`F/A/D/L/B/P/Q/M/I` 类型），命名与展平规则对齐 PlotJuggler `data_load_ulog`——`timestamp` 与 `_padding*` 不产出曲线、嵌套结构用 `.`、数组下标补两位 `.00`、`char[N]` 当字符串不当数值、同 topic 有 `multi_id>0` 时全部实例加 `.NN` 后缀。解析在扩展内完成；UI 按后缀分流，通道按 topic 分组挂树，双击出图。实测：自造 `tests/fixture/demo_flight.ulg`（250 Hz / 12 s / 4 topic）→ 13 通道 × 3000 点、解析 10 ms；PX4 官方 900 KB 样例 → **1351 通道 / 1812 行 / 解析 16.9 ms**。
- **CAN 双格式（ASC / BLF）已接**（2026-11-13，现为扩展侧 `AscParser` / `BlfParser` + `common/CanFrames`）：统一帧模型；ASC 解析经典帧 / 扩展帧 `x` 后缀 / `CANFD` 行 / `base hex|dec`；BLF 解析 `LOGG` 头 + `LOBJ` 对象流 + `LOG_CONTAINER` zlib 解压，对象跨容器时留半截续接。BLF 无公开规范，字段偏移按 python-can `io/blf.py`（源于 vector_blf）逐项对照。
- CAN 出通道**不猜物理量**：没有 DBC 就不知道哪几个字节拼成一个信号，硬猜的曲线是假的。只给两类可验证量——`CAN<总线>.<ID>.bN`（报文第 N 字节原始值，恒定字节直接丢弃，否则 500 个 ID × 8 字节会把通道树撑成几千行）与 `CAN<总线>.<ID>.period`（同 ID 相邻帧间隔 ms，看周期抖动最直接）。DBC 解码归 M2 协议编辑器（D13）。
- **对拍证据**：用 python-can 4.6.1 的 `ASCWriter`/`BLFWriter` 把同一批 3020 帧写成 `tests/fixture/demo_can.asc`（190 KB）与 `demo_can.blf`（19.5 KB），两种格式各自解析后**逐通道逐点相同**（C++ 扩展测试），帧数/每 ID 帧数也与 python-can `LogReader` 完全一致；解析耗时 ASC 0.9 ms、BLF 0.7 ms。
- 未接：JKQTPlotter（可选后端，未进构建）、Lua 5.4、NMEA/DFLog、DBC 解码、NDF 落盘、多 Y 轴、仪表自绘、`.psl`。

**M1 扩展自解析（2026-11-13，当前架构）**

- 内核只保留通用数据模型与算子：`ps_dataset_new / add_channel / channel_count / channel / free / apply_filter / preview_filter / apply_quat_rpy / remove_channel / lod_minmax`。`ps_kernel.h` 中的 `ps_csv_* / ps_ulog_* / ps_apbin_* / ps_asc_load / ps_blf_load` 已删除，`src/application/kernel/ps-parse` 已移出 workspace。
- 每个 loader 扩展自带 probe / parse / schema：`data_loader/common` 只做通用 dataset 构建与文件/JSON helper；格式细节不进内核。
- CSV：`CsvParser.{h,cpp}` 在扩展内完成分隔符探测、表头/列类型、时间列建议、9 行预览、`row/column/combined` 三种时间模式、非单调时间排序；`CsvDataLoader.cpp` 只把解析结果交给 `psBuildDataset`。
- APBin：`ApbinParser.{h,cpp}` 在扩展内实现 DataFlash `A3 95` / FMT 自描述 / FMTU 单位倍率 / UNIT / MULT / PARM，通道名 `MSG.#N.Field`，参数表由扩展输出 JSON。
- ULog：`UlogParser.{h,cpp}` 在扩展内实现 `F/A/D` + `P/Q`，展平/多实例/字符串跳过规则不变；`demo_flight.ulg` 仍为 13 通道。
- ASC / BLF：各自 `*Parser.{h,cpp}`，共用 `common/CanFrames` 的帧→通道规则（只保留变化字节 + `period`）；BLF 的 zlib 容器由扩展自己解压，内核不链 zlib。
- loader 选项 UI 的归属：`schema_json.fields` + `ui.preview` + `initial_from_probe` 由扩展提供，app 的 `DataSourceDialog` 只做通用渲染；app 源码不再出现 `delimiter / has_header / suggested_time`，新增格式不需要在 app 里加对话框。
- 数据源与 loader 共用 `DataSourceDialog`：`transport` 只声明 `{"supported":[...],"default":"udp://127.0.0.1:1223"}`，通信页由 app 按 transport 固定字段渲染（`file → filepath`、`udp → bind/port`、`tcp → host/port/timeout_ms`、`serial/uart → serial_port/baud`、`internal` 无字段），扩展不再提供 transport 字段；解析参数由扩展 `schema.parser.fields`（loader 用 `schema.fields`）提供，app 不解释具体解析字段；parse 可多选，每个 parse 展示自己的配置，只有一个 parse 或一种 transport 时不渲染对应选择器。
- 测试：`ctest` 6/6；`extension_file_formats` 覆盖 ULog 13 通道、APBin 通道+PARAM、ASC/BLF 各 10 通道；新增 `tests/fixture/demo_apbin.bin` 作为可复现夹具。

**Ribbon / Dock spike（2026-11-12 实测）**

- Ribbon：SARibbon 2.9.6 跑通（MIT、Qt 6.2.4、静态库单 target、24 MB 源码）。实测七页签 + 分组 + 右侧 qchip 位（`rightButtonGroup`）都可用；展开高度 **97 px**、折叠 **28 px**（原型 74 / 32 px）。静态库需 `Q_INIT_RESOURCE(SARibbonResource)`；观感偏 Office，要贴原型必须重写 QSS + 自绘图标。
- Dock：`QDockWidget` 直接够用（四区 + 浮动 + `tabifyDockWidget` + `saveState/restoreState`），零第三方许可义务；Qt-ADS（LGPL-2.1）留作 M1 升级项；KDDockWidgets 含 GPL，排除。
- 取舍：Ribbon 承认"多 23 px 换键盘导航/溢出折叠/无障碍"；QSS 压缩目标 ≤84 px。达不到就退回自绘（现有 `QTabBar + QToolBar` 骨架已在 `src/application/shell`）。

**已实装（2026-11-12 晚）**：SARibbon 2.9.6 源码裁到 3.3 MB（去掉 `docs/`、`example/`、`pyqt6/`、`pyside6/`、`sip/`、`tools/`、`tests/`）放进 `src/3dparty/saribbon`，`src/3dparty/CMakeLists.txt` 里强制 `SARIBBON_BUILD_STATIC_LIBS=ON` / `SARIBBON_BUILD_EXAMPLES=OFF` / `SARIBBON_USE_FRAMELESS_LIB=OFF`（frameless 要 QWindowKit 子模块，内网拿不到），`add_subdirectory(... EXCLUDE_FROM_ALL)`，app 链 `SARibbonBar::SARibbonBar`。`MainWindow::createRibbon()` 从自绘 `QTabBar + QStackedWidget(QToolBar)` 换成 `SARibbonBar`：7 页签 / 文件页三组（布局·导出·截图）/ 右侧状态 action。`RibbonStyleLooseSingleRow` + tab 22 px + panel 标题 10 px → **84 px**（原型 74 px，目标 ≤84 达成；同参数下 CompactTwoRow 94 px、CompactSingleRow 62 px 但组标题会挤压换行）。**已知缺口**：单行模式下每个按钮左侧留图标位，没图标就是空方块——必须补一套自绘 SVG 图标（约 22 个），否则观感不成立。

**M0-0 画布 spike（2026-11-12 实测，改判 D3）**

环境：Qt 6.2.4、4 vCPU 云主机、软件光栅、`QT_QPA_PLATFORM=offscreen` 下 `QWidget::render`；数据 20 通道 × 8000 点，视口 1500×900，预热 3 帧后取 30 帧均值。JKQTPlotter 5.0.0（HEAD `9e9f0af`）静态编译接入。可复跑脚本：`tests/perf/canvas_spike/`。

| 方案 | 单帧 |
|---|---|
| 自绘：Rust LOD → 1500 桶 + `drawPolyline`，AA 关、线宽 1.0 | **7.2 ms** |
| 自绘：同数据，AA 开 + `QPainterPath`、线宽 1.3 | 915 ms |
| JKQTPlotter：原始 16 万点/帧（AA 开关无差别） | 60–64 ms |
| JKQTPlotter：喂同样的 LOD（20×1500） | 13.1 ms |

1. 决定 60 fps 的不是图表库，是"1px + 关 AA + LOD"。→ 已写成 §11 铁律 4。
2. JKQTPlotter 过不了 ≤8 ms 的门（13.1 ms），但仍在 16.7 ms 帧预算内。所以它从"首选"降为"可选后端"：当我们确实需要它的坐标轴/图例/框选/导出时才接，且数据必须由 LOD 喂，不能把原始点扔给它。
3. 两条路都做不了抗锯齿平滑曲线（AA 下都超预算一个数量级以上）。真要平滑，换 `QOpenGLWidget`/QSG 批量上传顶点，另立 spike；否则接受 1px 折线这种工程软件惯用观感（PlotJuggler 也这么画）。

**安装布局**（安装器与便携版共用一套）

```
bin/PlotScope.exe
bin/extensions/*.dll          # 官方扩展
function/*.lua             # 可热改
share/psc/{ops,icons}/     # schema 与资源
docs/                      # 只放许可与第三方声明（原型与设计文档不进安装包）
```



| 里程碑 | 内容 | 工期 | 状态（2026-11-13） |
|---|---|---|---|
| **M0-0** | 选型 spike：JKQTPlotter vs Qwt，20 通道 × 8k 点 @60fps，单帧 ≤8 ms | **0.5 天** | **已完成**：自绘 7.2 ms；JKQ 13.1 ms |
| **M0** | 骨架：按 §12 目录落地、CMake + Rust 内核接通、窗口/停靠/页签、CSV 加载、NDF、单画布 | 3 周 | **已完成** |
| **M1** | ULog + DFLog + CAN 家族 + MAVLink；缓存层；扩展 ABI + 脚手架 | **3.5 周**（CAN 进则 6.5） | **基本完成**：ULog/DFLog/ASC/BLF、MAVLink UDP/raw/`.tlog` 回放、缓存、外部扩展 manifest/ABI 已落地；ASC/BLF 支持可选 DBC 信号解码 |
| **M2** | 派生通道增量算子、脚本、MDF4、GNSS 解析、实时滚动面板自绘 | 4 周 | **部分完成**：12 算子、四元数、Lua 5.4、OEM7、NMEA/UBX 已落地；MDF4、自绘滚动面板未做 |
| **M3** | MCAP/rosbag2、多画布/多轴、时间对齐、视频同步 | 4 周 | **部分完成**：MCAP 加载/录制、多画布、时间对齐、rosbag2 `.db3`、双 Y 轴、视频同步已落地 |
| **M4** | 协议编辑器（Loader 的 options 子窗口：位域/CRC/联动）+ 模板编译 | 3 周 | **部分完成**：`.psproto` 固定/变长帧、位域、CRC16、模板编辑器、字段-JSON/hex 联动已落地；模板热编译未做 |
| **M5** | 仪表页、布局 `.psl`、导出 | 2 周 | **完成**：图型仪表、独立仪表页（地平仪/罗盘/表盘）、`.psl`、PNG/CSV 导出 |

**页面/面板形态**：单窗口多页（页签挂在绘图区顶部，只跨绘图区宽度），每页一棵切分树；停靠面板可浮动；页数不做硬限制，按内存策略回收。

---

## 13. 待拍板（8 条）

| # | 事项 | 影响 | 我的建议 |
|---|---|---|---|
| 1 | **CAN 家族是否进 M1** | M1 3.5 ↔ 6.5 周 | 若"车辆"是首期市场就进，否则推 M2 |
| 2 | 时间对齐（多源 time sync）是否进 M2 | 2~3 周 | 进。这是本域最高频诉求 |
| 3 | 视频与曲线同步回放 | M2 末 vs M3 | M3，先保证曲线 |
| 4 | 脚本语言范围 | M1 表达式（Lua 语法糖）vs 同开完整 Lua | 先上表达式（自动包成 Lua），M2 开完整脚本 |
| 5 | 协议编辑器位域深度 | 全做 +1 周 | 全做（跨字节位域 + CRC 自动校验是我们的卖点） |
| 6 | 与 LCMS 的边界 | 是否共用登录/权限/文件服务 | 只共享"项目/文件"概念，不共用实现 |
| 7 | 开源节奏 | 一次性开源 vs 里程碑开源 | 里程碑开源（M1 后先发加载器 + 绘图） |
| 8 | enum 字典所有权 | 文件里有就用文件，YAML 只能覆盖/加别名？ | 是。文件优先，YAML 覆盖 |

---

## 14. PJ4 对标补全计划

> 基线：`488fea9`（多数据源 + 源别名图例 + 布局持久化）；修订依据：`pj4-alignment.md`。v1.0 中“派生数组归 UI、四元数算内置、低通算 PJ 能力”等判断已作废。

**原则**：
- UI 保持我们自己的深色工作台，不照搬 PJ 的 Qwt 观感。
- 内部机制按 PJ4 的分层来：数据存储 → 处理器 → 派生曲线 → 绘图适配。
- 变换非破坏性：原始曲线保留为 ghost，滤波/派生结果单独成曲线。
- 先内置算子，后脚本。PJ4 的内置过滤器本身是 Luau 类；我们先 C++ 实现同语义接口，后续可替换为 Luau。
- 每阶段必须可运行、可截图、有测试，不做半成品入口。

---

### 14.1 现状

已完成：
- ULog / CSV / ASC / BLF 加载，单源字段/参数/消息。
- 多数据源并行：每源独立 `PsDataset`，切源不释放，删源只清该源曲线。
- 曲线图例带源别名，源可重命名。
- 页签/切分/多画布、拖拽/双击加曲线、缩放/平移、X 联动。
- 曲线右键：显隐/颜色/线宽/删除；画布右键：添加曲线/自适应/缩放/增删画布。
- `.psl` 保存/导入页面树、曲线、时间窗、Y 范围、停靠、窗口、全部源路径；启动恢复。
- 设置：启动恢复布局、默认网格/图例。

主要缺口：
- 没有任何数据变换/派生曲线。
- 多源只有“同时打开”，没有时间对齐、同名通道配对对比。
- 时间游标/读数、CSV 导出、脚本、实时源、扩展机制缺失。

---

### 14.2 目标架构

```
ps_kernel (Rust)
  raw dataset / channels / LOD
        │ ps-abi
SourceData (host, MainWindow)
  PsDataset* + alias + displayOffsetSec + derived registry
        │
transform::Processor (C++ v1)
  id / params / numInputs / numOutputs / reset / calculateNextPoint / applyBatch
        │
DerivedSeries
  recipe + owned samples + dirty/lazy recompute
        │
PlottingCanvas::Series
  raw | derived，ghost 样式，图例显示 “源 · 通道 → 变换”
```

#### 14.2.1 处理器接口（对齐 PJ `pj_datastore/data_processor.hpp`）

```cpp
struct Sample { double t; double v; };

class Processor {
public:
    virtual QString id() const = 0;          // 目录键
    virtual QString label() const = 0;       // 图例后缀，如 "MA(100)"
    virtual int numInputs() const { return 1; }
    virtual int numOutputs() const { return 1; }
    virtual void reset() = 0;
    virtual bool next(const Sample &in, Sample &out) = 0;   // false = 抑制该行
    virtual void applyBatch(const QVector<Sample> &in, QVector<Sample> &out);
    virtual bool failed() const { return false; }
};
```

要点照 PJ：
- 顺序契约：按时间升序逐样本调用，状态跨样本保留，只有 `reset()` 清状态。
- 派生结果按 PJ 的方式物化成数据集里的普通通道/主题，而不是只活在 UI 数组里；多画布、导出、布局都按普通通道引用。
- 预览阶段用“新建处理器 + 全量重算”，不复用累加器；Apply 后输出替换原曲线并继承颜色，原始数据仍在数据树。
- 多输入（四元数）先做 4→3，时间戳必须同拍；不同拍按最近邻 join，并记录告警。

#### 14.2.2 派生曲线

```json
{
  "id": "ma_roll",
  "source": "/path/flight1.ulg",
  "inputs": ["attitude.roll"],
  "filter": { "id": "moving_average", "params": { "window": 100 } }
}
```

- 给 `ps_kernel` 增加派生通道写入：同一 `PsDataset` 下新增普通通道（名字如 `attitude.roll.abs`），拥有独立 t/v 数组。
- 画布 `Series` 增加 `derivedChannel` 引用；图例显示 `flight1 · attitude.roll · Absolute`。
- 预览时 before 曲线画虚线 ghost；Apply 后用输出替换来源曲线并继承颜色（对齐 `PlotWidget::replaceCurve`）。原始通道保留在树里，可重新拖回。
- 派生通道参与右键菜单：编辑变换、移除变换、导出数据。
- 配方写入 `.psl` v2 的 `derived` 根节点（id、源路径、输入通道、processor_id、params、输出名）；导入时先重建通道再绑曲线。
- 链式：对派生通道再次应用过滤器即可，不做单独的链编辑器。

#### 14.2.3 内置算子（首批，ID 与 PJ4 对齐）

| id | 说明 | 参数（对齐 PJ） |
|---|---|---|
| `none` | 直通 | — |
| `absolute` | 绝对值 | — |
| `scale` | 比例/偏移/时间平移 | `value_scale`, `value_offset`, `time_offset_sec` |
| `derivative` | dv/dt | `use_custom_dt`, `custom_dt` |
| `integral` | 梯形积分 | `use_custom_dt`, `custom_dt` |
| `moving_average` | 滑动平均 | `window`, `compensate_time_offset` |
| `moving_rms` | 滑动 RMS | `window` |
| `moving_variance` | 滑动方差/标准差 | `window`, `std_dev` |
| `outlier_removal` | 单点尖峰剔除 | `outlier_factor` |
| `samples_counter` | 窗口内样本数 | `samples_ms` |
| `binary_filter` | 阈值二值化 | `binary_op`, `binary_a`, `binary_b`, `binary_tol` |
| `time_since_previous` | 相邻样本 Δt | — |
| `normalize` | 归一化 0–1 | — |

算子目录唯一来源：`src/application/shared/filter/include/filter/FilterCatalog.h`；datasource 派生通道与 plotting 曲线 filter 共用该目录，求值统一走内核 `ps_filter_apply_series` / `ps_preview_filter`，UI 不自带算子分支。

四元数不在 PJ4 内置目录里，而是官方 Quaternion 扩展：4 输入（w,x,y,z）→ 一个分组 topic `rpy:roll,pitch,yaw`。当前数据模型是扁平日通道，先实现为 `rpy.roll/.pitch/.yaw` 三条通道；支持 degree、unwrap、pitch ±π/2 截断。

语义细节按 PJ `resources/filters/builtin_filters.luau`：
- moving_average 不足窗口时用当前值补齐，除以完整窗口；可选按缓冲跨度居中时间戳。
- derivative / integral 首个样本抑制，integral 用梯形法。
- outlier_removal 只丢单点尖峰，不连续。
- samples_counter 输出 int64。
- scale 的时间平移作用在输出时间戳，不改输入。

#### 14.2.4 多源对比

- 每个 `SourceData` 增加 `displayOffsetSec`：显示时间 = raw − offset；再预留全局 reference（PJ 的 `sourceDisplayOffset` 与 `globalTimeReference` 分层）。
- 源行右键/底部源时间轴：拖动偏移、Align starts / centers / ends、Reset all。
- 加载时提供 prefix/别名，同名 topic 用前缀区分（PJ 的 `recordLoadedSource(prefix)` 语义）。
- “对比同名通道”：选中两个源，按通道名配对，一次把两源同一通道加到当前画布，颜色分源。
- 布局保存每源 offset、别名、时间轴顺序。
- 真正的合并不急：PJ 的 `mergeDatasets` 是破坏性的 anchor 合并，先把显示对齐做对，再决定是否实现。

---

### 14.3 阶段与验收

#### 14.3.1 P0 变换内核 + 派生曲线（已完成）
任务：
1. 数据源侧派生通道：`src/application/modules/datasource/dialog/TransformDialog.{h,cpp}`，内核侧 `Processor`/`Catalogue`/`SisoFilters`/`QuatToRpy`/`DerivedSeries`。
2. 实现 12 个 SISO + 四元数 4→3，参数用 JSON 描述，目录可查询。
3. 字段树右键“添加变换…”；面板式编辑器：输入通道、算子、参数表单、预览（before 虚线 + after 实线）、Apply。
4. 输出作为普通派生通道进树（每源一个“派生”分组）、进画布、可编辑/移除。
5. Apply 替换来源曲线并继承颜色；不保留 ghost。
6. `.psl` v2 保存派生 recipes，导入重建。
7. 测试：`tests/transform_golden.cpp`，逐算子比对首/末/数量与关键点；四元数用已知姿态（0/90/45°）验证。

验收：
- `attitude.roll` 加 MA(100) 后输出替换原曲线，颜色不变；重新拖原始通道可对比。
- `attitude_quat` 的 w/x/y/z 生成一个分组 `rpy`（roll/pitch/yaw 三列），数值与 PX4 公式一致。
- 保存布局重启后派生曲线和参数原样恢复。
- ctest 全绿，过滤算子有 golden 数值。

#### 14.3.2 P1 多源对比 + 时间对齐（已完成）
任务：
1. `SourceData.displayOffsetSec`，画布 X 统一走 `displayTime()`。
2. 源行右键“时间偏移”“设为基准”“对比同名通道”。
3. 对比动作自动配对并着色，图例 `源A · x` / `源B · x`。
4. 时间游标：竖线 + 各曲线当前值读数。
5. 布局保存 offset。

验收：两次仿真的 `attitude.roll` 叠在一张画布，能手动对齐起点；重启后偏移保留。

#### 14.3.3 P2 导出与测量（已完成，2026-11-14）
任务：
1. 导出数据 CSV：原始/派生/当前时间窗，列名带源别名。
2. 单画布导出 PNG、复制曲线数据到剪贴板。
3. 区间测量：A/B 游标、Δt、Δy、斜率。
4. 设置补充：时间格式、采样上限、派生重算上限。

验收：CSV 可被 Excel 打开，列名不歧义；测量值与数据一致。

本轮收口：
- 当前源 CSV 表头改为 `time,source,channel,value,unit`，`source` 写源别名。
- 设置页增加时间轴格式（相对秒 / 时分秒 / UTC）、CSV 导出每通道采样上限、派生通道重算上限；后两项 0 表示不限。
- 采样上限按通道均匀抽点，日志记录实际上限；派生上限在 `TransformDialog` 与 `.psl` 重放两处生效，超限拒绝并给原因。

#### 14.3.4 P3 自定义函数编辑器（Lua 5.4，部分完成）
不照搬 PJ 的 Lua Editor 界面，能力对齐：
1. `ps-script` 接 Lua 5.4（mlua，vendored），沙箱禁 `io/os/package`，步数/内存/单块时间预算。
2. 契约：`init(params)`、`reset()`、`step(t, value, v1..v3)` 返回 number 或分组 table；支持 1–4 输入、单/多输出。
3. UI：代码编辑、输入绑定、参数表单、before 虚线 + after 实线预览、Apply 替换、错误行号。
4. 内置片段：`quat_to_rpy`、derivative、integral、clamp、rad2deg、moving_average。
5. 源码随 `.psl` 保存；导入时重建通道并重新编译。
6. 兼容：先保证 PJ3 Lua 脚本可改写运行；Luau 语法差异后续加适配层，不阻塞。
7. Python 后置，编译开关控制。

验收：写一个 4 输入四元数脚本能出 roll/pitch/yaw 并绘图；错误脚本只报错不崩 UI；重启后脚本和参数恢复。

本轮落地与差异：
- 实际接的是 PUC Lua 5.4 C API（`liblua5.4-dev`），不是 `mlua`；仍是进程内沙箱，禁 `os/io/package/debug`，20M 指令预算 + 64 MB allocator。
- API 采用即时计算式 `ps.ch/ps.time/ps.plot/ps.movingAvg/ps.derivative/ps.integral/ps.scale`，并增加 `ps.input(1..4)` 与 `v1..v4`。
- 已实现 `init(params)` / `reset()` / `step(t, v1..v4)` 逐点协议：`step` 返回 number 或 `{name=number}` 表，按名字聚合成序列；存在 `step` 时优先于即时式输出。
- 脚本面板增加 1–4 输入绑定下拉；运行时按绑定顺序映射为 `v1..v4`，未绑定则回退当前画布全部曲线。
- 语法错误包含行号，面板自动选中出错行；源码与绑定写入 `.psl` 的 `script` 对象，导入时恢复。
- 输出追加到当前工作区画布；已修复 `ps.plot` 与全局 `out` 重复输出（改用 `lua_next` 判断空表，`lua_rawlen` 对字符串键表恒为 0）。
- 已做：参数表单（名称 + 数值）传入 `init(params)`，随 `.psl` 的 `script.params` 保存。
- 已做：预览/应用分两步。预览输出 `preview/<name>` 叠加在原曲线旁（before/after），再次预览先清旧预览；“应用”按输入绑定顺序替换来源曲线、继承原颜色与单位，原始通道仍在字段树里，可重新拖回。
- 未做：Luau 适配。
- 已做：Python 3 入口（源码首行 `# python` 自动识别，stdin 传 inputs/params JSON，stdout 收 outputs；缺少 python3 或超时按脚本错误返回）。

#### 14.3.5 M1 加载器 schema 化（宿主内注册已完成，外部 ABI 未做）

已落地：
1. `src/application/modules/datasource/include/datasource/DataSourceEntry.h` 定义统一数据输入描述：公共字段 `id/version/label/schema/capabilities`，loader 侧 `filter/formatTag/tree/hasParams/probe/defaultOptions/load`，source 侧 `kind/transport/open/poll/close`；不再保留 `LoaderExtension` / `SourceExtension` 别名。
2. `LoaderRegistry` 注册 ULog、ArduPilot DataFlash、ASC、BLF、CSV；按后缀匹配，`loadFile()` 不再按扩展名写 `if/else`。
3. `DataSourceDialog` 完全由 schema 渲染：`enum/bool/int`、`visible_if`、动态列（`source=columns` + `column_filter`）、探测预览表、警告；新增格式只需注册 schema，不再在 app 里加对话框。
4. CSV schema 覆盖分隔符、表头、跳行、`time.mode=row|column|combined` 及时间列选择。
5. 删除 `src/application/shell/CsvLoaderDialog.*`。

已补（2026-11-13 复核）：
1. 外部扩展 `.json` manifest 发现 + 动态库 ABI 已落地（`ExtensionRuntime` / `ps_extension_host`，DemoExtension 回归），`PS_EXTENSION_ABI_VERSION=6`。
2. MAVLink UDP 实时流、OEM7 TCP/串口、Sim internal 已接入；MCAP 文件加载已落地；实时记录已落地（CSV/MCAP/RAW）。
3. 实时环形缓冲由 kernel `PsStream` 承担，`LiveSession` 每通道 262144。
4. OEM7 RAW 录制时，`StreamRecorder` 从配置 `database` 读取协议库字节写入 PSRAW1 `schema_blob`；回放不再依赖外部 database 路径。

未完成：
1. 多 parse 共享同一 transport 的运行时（当前只启动入口 parser；`dataInputJson()` 已生成但未消费）。
2. MAVLink tlog 原生解析、ROS1/ROS2、MDF4、NMEA/UBX 原始解析。
3. RAW 录制对 OEM7 尚未把 `database` schema_blob 写进宿主 raw 头；回放需配置 database 路径。

#### 14.3.6 UI / 画布本轮补齐

1. **任意画布布局**：页内改为 `QSplitter` 树；右键/绘图 Ribbon 支持“向右拆分/向下拆分”，拆分方向不同时自动嵌套；`m×n` 只作为一次性快捷布局，落地后仍可继续拆。
2. **逐曲线游标**：画布十字线对每条可见曲线二分查最近样本，框内列出 `label + value`，时间已扣源偏移。
3. **A/B 测量**：右键开启后点两下打 A/B 标线，显示 `Δt`，逐曲线 `Δy` 与斜率 `k`。
4. **导出**：右键“导出 PNG…”“导出当前画布 CSV…”；CSV 为长表 `time,source,channel,value,unit`，只导当前可见时间窗。
5. **Ribbon 图标优先**：大/小按钮只留图标 + tooltip；新增网格、边框、图例、框选、放大/缩小、左右/上下拆分图标。
6. **Dock**：标题栏可拖动浮动，拖到主窗口边缘吸附停靠；双击切换浮动；`—` 收成 26px 竖排页签，点击恢复。仍是 Qt 原生 `QDockWidget` 四区，未做 VS 式拖拽引导框/面板分组。

#### 14.3.7 P4 图型 / 数据源 / 扩展 / 渲染

已落地（本轮）：
- `PlottingCanvas::PlotMode`：时间序列、XY、散点、柱状、直方图、频谱 FFT、3D 轨迹、地平仪、罗盘、表盘。
- 绘图 Ribbon 画廊 11 个入口全部落到真实画布，不再建 `QLabel` 占位页；新图页沿用当前画布可见通道。
- XY 以首条通道为 X、其余为 Y；直方图按首条通道分桶；FFT 对首条通道做 512 点 DFT；仪表类按通道名/末值取数；3D 轨迹用正交投影。
- 图型随 `.psl` 保存（`mode` / `modeTag`）。

已落地（2026-11-13 复核）：
- MCAP 文件加载（`McapReader` + `data_load_mcap`，含 zstd/lz4 chunk）；MCAP 录制（`StreamRecorder` + 最小 `McapWriter`，无 chunk/summary，标准 Python `mcap` reader 可读）。
- MAVLink UDP、OEM7 TCP/串口、Sim internal 实时源；CSV/MCAP/RAW 三种统一录制；Record 下拉框由 `DataSourceDialog` 渲染。
- 扩展 SDK：loader/source 共用 `DataSourceExtension`，外部 `.json` manifest + 动态库 ABI 已落地。

未落地：
- Parquet、ROS2、CBOR/MessagePack、MDF4；通用 JSON/JSONL 已由 `data_loader_json` 支持。
- MQTT/ZMQ/WebSocket/LSL/Foxglove/PJ Bridge/WebRTC。
- 扩展市场、streamer/parser 两类独立 SDK 扩展。
- 3D 曲面、GPS 地图、GPU 渲染与百万点基准。

---

### 14.4 工程约束

- 注释极简，只解释“为什么”，不写教科书。
- 处理器纯函数式优先；状态机必须 `reset()` 可复现。
- 派生曲线只在可见/被引用时重算，避免打开大日志就全量跑。
- 所有持久化字段带版本号；v1 布局升级到 v2 时保留旧字段兼容。
- 每阶段结束：构建、ctest、截图、提交 Gitee `main`。

### 14.5 参考的 PJ4 代码位置

- 处理器抽象：`pj_datastore/include/pj_datastore/data_processor.hpp`
- 内置过滤器与参数语义：`resources/filters/builtin_filters.luau`
- 惰性滤波曲线：`pj_plotting/core/include/pj_plotting/FilteredCurveAdapter.h`
- 多源合并/时间偏移：`pj_datastore/include/pj_datastore/merge_result.hpp`、`pj_runtime/include/pj_runtime/SessionManager.h`
- 脚本引擎：`pj_scripting/src/luau_engine.cpp`、`python_engine.cpp`
- 布局与曲线描述：`pj_plotting/core/include/pj_plotting/PlotXml.h`、`pj_runtime/include/pj_runtime/CurveDescriptor.h`
- 扩展与市场：`pj_marketplace/`、`plotjuggler_sdk`


### 14.6 实现进度（2026-11-13）

- 内核：`ps-ops/src/filters.rs` 13 算子（含 `normalize`）；`quat.rs` 4→3；`ps-abi` 新增 `ps_dataset_apply_filter`、`ps_dataset_apply_quat_rpy`、`ps_dataset_remove_channel`。
- UI：`TransformDialog` 双页签；字段树右键「添加变换… / 画到当前画布 / 删除派生通道」；`Ctrl+T`；产物进「派生通道」分组。
- 验证：`ctest` 2/2（含 CSV → MA(2) → 四元数闭环），AP 真实样本手测滤器与四元数。
- 已做：配方写 `.psl` `transforms[]`，导入时按源路径重放；删除派生通道同步移除配方。
- 已做：13 算子 golden（none/absolute/scale/MA/RMS/方差/计数/二值化/Δt）+ 四元数 degrees/unwrap/零模长测试。
- 已做：Filter Editor before/after 预览（`ps_preview_filter` 只读 ABI，不写数据集）。
- 未做：变换链可视化编排（派生通道可继续作为输入，`.psl` 按序重放）。
- 提交：`5475215`（P0 内核/ABI/对话框/配方）、`bf10624`（12 算子 golden + 样式收口）、`43fa0c8`（预览）。

### 14.7 2026-11-13 实时录制收口与计划复核

已落地：
- `StreamRecorder` 独立于 `LiveSession`：有界队列（4096 批 / 32 MiB）、后台写线程、磁盘慢时整批丢弃并计数；默认目录 `QStandardPaths::AppDataLocation/recordings`，不可写回退 `/tmp/plotscope-recordings`。
- 三种格式：CSV（`time,channel,value`）、MCAP（每采样点 JSON，`plotscope/value` schema 标记）、RAW（PSRAW1 原始字节）。
- `PsSourceSink` 增加可选 `raw_batch`，ABI 6；MAVLink/OEM7 raw 字节优先交宿主，未接宿主时保留扩展自带 `raw_path`。
- `DataSourceDialog` 录制格式下拉框；`MainWindow` 按 `__record_format` 启动；配置 JSON 传入 raw header。
- `data_load_mcap` 按 `schema.name == plotscope/value` 还原单值通道，普通 MCAP 的 `{"value":x}` 不再被误判。
- 测试：`ctest` 14/14；新增 `test_stream_recorder`；标准 Python `mcap` reader 可读录制文件。
- 提交：`6f56ca9 feat(app): 实时数据流统一录制 StreamRecorder（CSV/MCAP/RAW）`。

仍未完成：
- 多 parser 运行时：入口扩展独占 transport，其余 parser 以 `transport=external` 打开，宿主把 `raw_batch` 扇出；通道名加 `fanout.<parserId>.` 前缀。
- GUI 交互冒烟未做；仅 `QT_QPA_PLATFORM=offscreen` 启动存活验证；raw 回放端到端未实测。
- MCAP writer 已按 1 MiB 聚合 Chunk，summary 重复 Schema/Channel 并写 ChunkIndex + SummaryOffset。

### 14.8 2026-11-14 P2/P3 收尾

已落地：
- 设置：时间轴格式、CSV 导出采样上限、派生重算上限；画布设置保存后即时刷新。
- CSV：当前源导出带 `source` 列与源别名。
- P3：Lua 1–4 输入绑定、`init/reset/step`、错误行定位、源码与参数随 `.psl` 保存；`init(params)` 接收参数表单；预览/应用分两步；Apply 替换来源曲线并继承颜色；Python 3 脚本入口（`# python` 标记，JSON 管道）。
- 多 parser：`PsDataInputVtable` / `PsDataSourceVtable` 尾部增加 `feed_raw`；MAVLink/OEM7 支持 `transport=external` 只解析不建连接；`MainWindow::openLiveInput()` 负责扇出与句柄回收。
- M3：`McapWriter` 增加 1 MiB Chunk 聚合、ChunkIndex、SummaryOffset 与 summary Schema/Channel 副本；`McapReader` 原有 Chunk 解析路径直接复用。
- M3：视频同步面板：ffmpeg 抽帧（10 fps/640px）到临时目录，播放/拖动时向所有画布推送 playhead，时间序列页自动滚动窗口；支持时间偏移。
- M3：PlottingCanvas 支持曲线绑定右轴（右键「绑定右轴」），左右轴独立量程/刻度，布局持久化；Ctrl+滚轮与纵向平移同步作用于双轴。
- M3：新增 `data_loader_rosbag2` 直读 `.db3`（SQLite topics/messages + CDR），支持 Float64/Float32/Int32/Int64/Bool/String/Imu/Twist/PoseStamped/Odometry/NavSatFix/Header；未识别类型只出 `payload_bytes`。rosbag2 目录也可加载。
- P4：GpsMap 图型：经纬度网格、轨迹连线、起终点标记，无在线瓦片依赖。
- P4：Surface3D 图型：多序列按 Z 轴堆叠的瀑布曲面，替代原 XY 占位。
- M4：`data_loader_frame` 支持固定帧与变长帧（`length` 偏移/字节数/端序/是否含头）；`ProtocolEditorDialog` 点击字段定位 JSON 与 hex 字节。
- P4：新增 `data_loader_rosbag2`（`.db3` 常见消息）。
- P4：新增 `data_loader_binary`，按扩展名解析 CBOR（`.cbor`）与 MessagePack（`.msgpack/.mpk`），数值字段扁平为 `cbor.*` / `msgpack.*`，支持数组批量记录与连续多条记录。
- M1：MAVLink `.tlog`（8 字节时间戳 + 帧）已由同一 data_source_mavlink 扩展原生回放，文件扩展名加入 `.tlog`；OEM7 raw 扩展名加入 `.psraw/.raw`。
- 本机依赖固定到 `deps/`，当前主界面截图 `docs/screenshots/PlotScope-Overview.png`。
- M1 CAN：ASC/BLF 加载器新增可选 `dbc` 配置，按 `BO_`/`SG_` 解码为 `CAN<bus>.<message>.<signal>`；留空仍导原始字节。CMake 公共库加 `Dbc.cpp`，测试覆盖 `CAN2.MSG_A.byte0`。
- M2 GNSS：新增 `data_loader_nmea`（GGA/RMC → `nmea.gga.*`/`nmea.rmc.*`）与 `data_loader_ubx`（NAV-PVT/NAV-POSLLH → `ubx.*`）；时间均归零到首样本，扩展 `.nmea/.txt/.ubx`。MDF4 仍未做。
- M5：仪表大屏从占位改为三画布（Horizon/Compass/Gauge），接线与普通画布一致；截图 `docs/screenshots/PlotScope-Instrument-Gauge.png`。
- P4：新增 `data_loader_json`，支持 JSON 数组/对象、JSONL/NDJSON、长表 `{time,channel,value}`，数值字段扁平为 `json.*`；CBOR/MessagePack 仍未做。
- RAW：OEM7 `database.json` 写入 PSRAW1 `schema_blob`。
- 测试：`ctest` 15/15，新增 `test_script_engine`（`ps.input`、`ps.input(5)` 报错、`step` 聚合）。
- 启动：`QT_QPA_PLATFORM=offscreen` 运行 8 秒存活；Xvfb 下真实 xcb 窗口启动并截图，非交互冒烟。
- 依赖：Qt 6.2.4 / Lua 5.4.4 固定到仓库 `deps/`（gitignore），`tools/bootstrap-deps.sh` 可重建，`tools/build-local.sh` 不读系统 Qt。
---

## 附录 A：必须守住的不变量（摘 11 条）

1. 一个 `Chunk` 内所有通道共享同一根 `t`。
2. `gap` 位只表示"不可采信"，绝不表示 0，也绝不插值。
3. 同一 `applicationId` 的 enum 字典不可变。
4. LOD 桶必须按时间顺序、互不重叠、覆盖全部采样。
5. 任何变换若为非单调或多输入，进 LOD 前必须物化。
6. 跨 ABI 的数据必须批量、且所有权归属明确（含失败路径）。
7. 内核头文件不得出现任何 Qt 类型。
8. 布局文件缺通道时保留"幽灵曲线"，不静默删除。
9. 缓存指纹不匹配时必须**完整重解析**，不得部分复用。
10. 扩展加载失败必须可归因（ABI/SDK/平台/清单四类之一）。
11. 任何 UI 线程调用内核接口都必须是非阻塞的。

## 附录 B：术语

| 术语 | 含义 |
|---|---|
| NDF | PlotScope 自研列存中间态（`Chunk<8192>` + gap 位图 + LOD） |
| LOD | 细节层级；本项目中特指 min/max 分桶降采样 |
| L1/L2 | L1 显示单位（轴级）、L2 通道元数据变换（factor/offset） |
| `.psl` | 单次分析布局（JSON，可分享） |
| `.psc-workspace` | 项目级工作区（含数据源引用与缓存索引） |
| ghost curve | 布局引用的通道在本次数据中不存在时保留的空壳 |
