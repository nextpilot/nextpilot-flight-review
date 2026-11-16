# PlotScope

PlotScope 是面向无人机、嵌入式与车辆研发的桌面日志分析工具。它把飞行日志、CAN 总线和通用 CSV 放进同一个工作区，用于多架次/多次仿真对比、时间轴对齐、曲线叠加和常用数据变换。

当前版本以**离线日志分析**为主，不对外承诺未实测的性能数字。

![PlotScope 主界面](docs/screenshots/PlotScope-Overview.png)

## 适合谁

- 飞控/无人机研发：快速查看 PX4 ULog、ArduPilot DataFlash，定位姿态、控制量和传感器异常。
- 车载/嵌入式研发：把 CAN ASC/BLF、MDF4、CSV 放在同一时间轴上对照。
- 试验与测试：多次仿真或多架次并排比较，导出曲线和截图做记录。
- 需要二次开发的团队：按功能模块扩展数据源、画布和业务组件。

## 一次典型使用

1. 打开日志：`文件 → 打开文件`，或把文件拖进数据源面板。
2. 选通道：在数据源树中勾选要看的字段，双击或拖到画布。
3. 对比：同一通道来自不同源时，图例显示为 `源别名 · 通道名`；可做起点/中心/终点对齐。
4. 分析：用游标读值、A/B 游标算 `Δt`/`Δy`，或做微分、积分、滑动平均等变换。
5. 保存：布局、数据源、时间偏移和变换配方写入 `.psl`，下次直接恢复。

## 主要能力

### 数据源

- PX4 ULog `.ulg`、ArduPilot DataFlash `.bin`
- Vector CAN `.asc` / `.blf`
- CSV / TSV / TXT、MCAP `.mcap`、MDF4 `.mf4` / `.mdf4`
- NMEA / UBX GNSS
- MAVLink UDP、NovAtel OEM7 TCP/串口、内置仿真源
- 实时源录制：MCAP、PSRAW1 原始字节、CSV

### 绘图

- 时间序列、XY、散点、柱状、直方图、FFT 频谱、3D 轨迹、地平仪、罗盘、表盘。
- Ribbon + 可停靠面板 + 多页签绘图区；画布可自由拆分，也可按 `m × n` 铺网格。
- 框选缩放、滚轮缩放、拖动平移、双击复位、十字游标、A/B 游标。
- 单画布导出 PNG，或导出当前可见时间窗的 CSV 长表。

### 数据变换与脚本

- 绝对值、比例/偏移、时间平移、微分、积分、滑动平均、RMS、方差/标准差、尖峰剔除、阈值二值化等。
- 四元数 `w/x/y/z` 一次生成 `roll/pitch/yaw`。
- 内置 Lua 5.4 脚本面板，可在受限沙箱里做派生计算并直接绘图。

### 工作区

- 多绘图页、页内自由切分布局。
- 数据源、属性、脚本、日志停靠面板。
- `.psl` 保存数据源、别名、时间偏移、画布布局、图型与变换配方。

## 当前边界

以下能力尚未完成，README 不把它们写成已支持：

- 3D 曲面、GPS 地图尚未实现；3D 轨迹是轻量正交投影，不是完整 3D 引擎。
- 脚本输出不写回 `.psl`，重启后不会恢复。
- MAVLink tlog 原生解析、Parquet、MQTT/ZMQ/ROS2/WebSocket/LSL 尚未接入。
- MCAP writer 只写最小结构（无 chunk/summary/索引）；大文件索引性能未验证。
- OEM7 宿主 RAW 录制尚未把 database schema_blob 写进文件头，回放需在配置里给 database 路径。
- `.psl` 仍是 JSON v1，未迁移到 PJ4 的 XML 工作区格式。
- 浅色主题尚未完成，当前只有深色界面；计划见路线图。

## 文档

- 用户手册：[`docs/manual/`](docs/manual/README.md)
  - [快速开始](docs/manual/getting-started.md)
  - [支持的数据格式](docs/manual/formats.md)
  - [绘图与交互](docs/manual/plotting.md)
  - [变换、脚本与设置](docs/manual/operations.md)
- 开发文档：[`docs/develop/`](docs/develop/README.md)
  - [架构设计](docs/develop/architecture.md)
  - [编译与构建](docs/develop/building.md)
  - [开发约定](docs/develop/development.md)
  - [测试](docs/develop/testing.md)
- 路线图与方案讨论：[`docs/roadmap/`](docs/roadmap/README.md)
- UI 原型：[`docs/prototype/PlotScope-工作区原型.html`](docs/prototype/PlotScope-工作区原型.html)
- 界面截图：[`docs/screenshots/`](docs/screenshots/)

## 许可

BSD-3-Clause。三方库许可见 `docs/` 与安装包 `third-party/`。
