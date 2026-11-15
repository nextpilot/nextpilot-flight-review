# PlotScope

PlotScope 是一个面向无人机、嵌入式与车辆研发的桌面日志分析工具。它把飞行日志、CAN 总线和通用 CSV 放到同一个工作区里，用于多架次/多次仿真对比、时间轴对齐、曲线叠加和常用数据变换。

当前版本以**离线日志分析**为主，不对外承诺未实测的性能数字。

![PlotScope 主界面](docs/screenshots/PlotScope-Overview.png)

## 能做什么

### 多源对比
- 同时打开多份记录，每个数据源独立持有通道、参数和消息。
- 源可重命名；同一通道来自不同源时，图例显示为 `源别名 · 通道名`。
- 支持起点、中心、终点对齐，也可手动设置时间偏移；偏移随布局保存。
- 一键“对比同名通道”，把两个源的同一字段叠到当前画布。

### 日志加载
| 格式 | 说明 |
|---|---|
| PX4 ULog `.ulg` | 按 topic/field 展开通道，可查看参数表 |
| ArduPilot DataFlash `.bin` | 按 `ATT.Roll` 等消息/字段展开，读取 FMTU 单位与 PARM 参数 |
| Vector CAN `.asc` / `.blf` | 无 DBC 时输出报文字节原始值与周期，不做物理量猜测 |
| CSV / TSV / TXT | 自动探测分隔符、表头和时间列；支持行号、指定时间列、日期+时间列合并 |
| MCAP `.mcap` | JSON channel，支持未压缩 / zstd / lz4 chunk；录制文件可直接回放 |
| MDF4 `.mf4` / `.mdf4` | 读取通道组与主时间轴，支持 `##DT` 原始块、`##DZ` zlib 压缩块（含转置压缩），以及线性/有理/多项式 `##CC` 转换 |
| NMEA / UBX | GNSS 组件按语句/消息展开通道，时间归零到首样本 |

加载器由 schema 驱动，新增格式不需要在 app 里单独写对话框。

### 实时源与录制
- MAVLink UDP（绑定地址/端口/sysid）、NovAtel OEM7 TCP·串口、内置仿真源；原始流回放按 PSRAW1 或旧裸字节兼容。
- 每个实时源由独立 `LiveSession` 持有环形缓冲与 UI 刷新；录制交给独立 `StreamRecorder`，不在 `data_input` 或会话里落盘。
- 录制格式三选一：MCAP（解析后通道）、RAW（PSRAW1 原始字节）、CSV（解码通道）；有界队列 + 后台写线程，磁盘慢时丢批并计数。

### 绘图
- 图型画廊覆盖时间序列、XY、散点、柱状、直方图、频谱 FFT、3D 轨迹、地平仪、罗盘、表盘；新建图页会沿用当前画布的可见通道。
- Ribbon + 可停靠面板 + 页签绘图区；面板支持拖动停靠、浮动、自动隐藏。
- 画布可向右/向下任意拆分，也可用 `m × n` 一次性铺好再继续拆。
- 网格、边框、图例、框选缩放、滚轮缩放、拖动平移、双击复位。
- 十字游标逐曲线显示当前值；A/B 游标显示 `Δt`、逐曲线 `Δy` 与斜率。
- 单画布可导出 PNG，或导出当前可见时间窗的 CSV 长表。

### 数据变换
- 绝对值、比例/偏移/时间平移、微分、梯形积分、滑动平均、滑动 RMS、滑动方差/标准差、尖峰剔除、阈值二值化、样本计数、相邻样本 Δt。
- 四元数 `w/x/y/z` 一次生成 `roll/pitch/yaw` 三条派生通道。
- 变换前可预览 before/after，应用后替换来源曲线并继承颜色；变换配方随 `.psl` 保存。

### 脚本
- 内置 Lua 5.4 脚本面板，提供 `ps.ch`、`ps.time`、`ps.plot`、`ps.movingAvg`、`ps.derivative`、`ps.integral`、`ps.scale`。
- 脚本在受限沙箱中运行：无 `os/io/package/debug`，带指令预算与内存上限；输出直接追加到当前工作区画布。

### 工作区
- 多绘图页、页内自由切分布局、数据源/属性/脚本/日志停靠面板。
- `.psl` 保存数据源路径、别名、时间偏移、画布布局、图型与派生配方，启动时可恢复。

## 当前边界

以下能力尚未完成，README 不把它们写成已支持：

- 3D 曲面、GPS 地图尚未实现；3D 轨迹是轻量正交投影，不是完整 3D 引擎。
- 脚本输出不写回 `.psl`，重启后不会恢复。
- MAVLink tlog 原生解析、Parquet、MQTT/ZMQ/ROS2/WebSocket/LSL 尚未接入。
- MCAP writer 只写最小结构（无 chunk/summary/索引）；标准 reader 可读，大文件索引性能未验证。
- OEM7 的宿主 RAW 录制尚未把 database schema_blob 写进文件头，回放需在配置里给 database 路径。
- `.psl` 仍是 JSON v1，未迁移到 PJ4 的 XML 工作区格式。

## 构建

依赖：CMake ≥ 3.22、Qt 6 Widgets、Rust 工具链（`cargo` 在 `PATH` 或通过 `-DPS_CARGO=` 指定）。

云主机/开发机可把 Qt 与 Lua 固定到仓库 `deps/`（已 gitignore，不随提交走），避免系统包被环境快照回退：

```bash
tools/bootstrap-deps.sh   # 下载 Qt 6.2.4 + 编译 Lua 5.4.4 到 deps/
tools/build-local.sh      # 只用 deps/ 的 Qt/Lua 构建并跑 ctest
```

zlib/zstd/lz4 缺失时由 CMake 自动从 Gitee 镜像拉源码静态构建，不访问 GitHub；已有系统库或 vcpkg 时优先复用。也可以 `-DPS_DEPS_ROOT=<prefix>` 指向已安装的 `include/` + `lib/`。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/src/base/plotscope_app
```

只构建 Rust 内核、不构建 GUI：

```bash
cmake -S . -B build -DPS_BUILD_GUI=OFF
cmake --build build -j
```

## 架构分层（基本规则）

PlotScope 按**功能模块**组织，不按 UI / core / service 这类技术类型分层。依赖方向固定，箭头指向被依赖者：

```text
extensions（业务固件） → modules（外设 / 通用能力） → shell（核心板 / 最小通用 App）
```

| 层 | 负责 | 明确禁止 |
|---|---|---|
| `extensions` | 具体业务：格式加载器、仿真/MAVLink、GNSS/飞控/汽车业务、协议、设备调试、视频、派生/滤波、Lua 脚本 | 被 `modules` 或 `shell` 反向 include / link |
| `modules` | 通用能力：`data_source` 数据源抽象与生命周期、`plotting` 画布/页面/轴/曲线、Dock/Ribbon 贡献 | include `extensions`；把业务类型写进通用层 |
| `shell` | 最小通用 App：`MainWindow`、页签容器、Ribbon/Dock 骨架、日志、命令注册 | include 或 link `data_source` / `plotting` / `extensions`；持有数据源、画布、业务状态 |

硬性约束：

- `shell` 不认识数据源、画布和任何业务；数据源与绘图状态必须由对应 module 的 controller 持有。
- 模块之间通过 contribution、command、registry 交互，不直接互相调用业务对象。
- 新增能力先判断归属：通用数据源/画布进 `modules`，具体格式或行业逻辑进 `extensions`，只有真正通用的 App 骨架才进 `shell`。

## 目录

```text
src/base/modules/shell/        核心板：入口、MainWindow、Ribbon/Dock 骨架、日志
src/base/modules/runtime/      组件注册、扩展生命周期、通用贡献接口
src/base/modules/data_source/  数据源抽象、加载器注册、实时源、缓存、数据源 Dock
src/base/modules/plotting/     画布、绘图页、轴/曲线、属性面板、脚本画布
src/base/kernel/               Rust 内核（解析、缓存、流、ABI）
src/extensions/builtin/        内置业务固件
src/extensions/dynamic/        外部扩展与各格式加载器
src/3rdparty/                  锁版本三方库（SARibbon 等）
docs/                          设计文档、PJ 对齐笔记、原型与截图
```

## 文档

- `docs/PlotScope-架构设计与PJ4实施-定稿-v1.0.md`：架构决策、模块边界、P/M 计划与实施进度。
- `docs/PlotScope-PJ4对齐与源码核对-v1.1.md`：PJ3/PJ4 能力矩阵与源码核对。
- `docs/prototype/PlotScope-工作区原型.html`：单文件 UI 原型，浏览器直接打开。

## 许可

BSD-3-Clause。三方库许可见 `docs/` 与安装包 `third-party/`。
