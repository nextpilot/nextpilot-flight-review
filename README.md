# PlotScope

**开源的时序数据绘图与分析桌面工具**——为飞控、机器人、汽车与卫导领域的日志回放、实时监控和事后分析而生。

飞控日志、CAN 报文、台架测量、GNSS 语句、普通 CSV，各自时间轴不同、格式不同、工具链割裂。PlotScope 把它们放进同一个工作区：统一加载、对齐时间、叠加绘图，再配上变换、脚本与报告导出。

![PlotScope 主界面](docs/screenshots/PlotScope-Overview.png)

> PlotScope 的产品形态与交互设计**参考和借鉴了 [PlotJuggler](https://github.com/facontidavide/PlotJuggler) 与 [LabPlot](https://labplot.kde.org/)**：前者定义了"把时序数据拖进画布就出图"的轻快体验，后者示范了科研级绘图的工作区组织。PlotScope 在此基础上面向嵌入式/领域日志做了自己的取舍——Rust 数据内核、领域工具箱、headless 优先。

## 产品特色

- **一个工作区，全部格式**——PX4 ULog、ArduPilot DataFlash、Vector BLF、MDF4、ROS2 bag、CSV/TSV、NMEA、自定义顺序协议，不再为每种日志装一个专用工具。
- **百万点不卡**——数据本体在 Rust 内核（列式存储），绘图走 min/max LOD 金字塔，内存占用与数据行数无关、只与像素宽有关。
- **多源对齐对比**——源别名 + 起点/中心/终点对齐 + 手动偏移，两架次数据叠加在同一画布。
- **GUI、CLI、脚本同一条路**——界面点击、命令行、Lua 脚本、MCP 外部代理，全部经同一条意图管道，可录制可回放。
- **领域知识可插拔**——解析器、面板、脚本、报告模板按领域打包在工具箱中，装卸领域包不动宿主。

## 核心能力

|            |                                                                                                                                                  |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| **数据源** | 文件：ULog / DataFlash / BLF / MDF4 / rosbag2 / CSV / 自定义协议 / NMEA·UBX；实时：MAVLink UDP、NovAtel OEM7、内置仿真；录制输出 MCAP / 原始字节 |
| **绘图**   | 时间序列、XY、散点、柱状、直方图、FFT 频谱、3D 轨迹、地平仪、罗盘、表盘；多页签 + 画布网格拆分；十字游标、A/B 测量（Δt/Δy/斜率）、书签           |
| **分析**   | 滑动平均/RMS/方差、微分/积分、时间平移、尖峰剔除、四元数转欧拉角；Lua 脚本工作台（编辑器/调试器/REPL）；headless 批量处理                        |
| **工作区** | `.psl` 保存数据源引用、别名、时间偏移、画布布局、图型与变换配方                                                                                  |

如实说明当前边界：3D 曲面与 GPS 地图尚未实现；浅色主题未完成；CAN DBC 解析在规划中。完整清单见[支持的数据格式](docs/manual/formats.md#已知限制)。

## 快速上手

### 图形界面

```bash
./build/src/application/gui/plotscope-gui
```

文件菜单导入日志 → 数据树双击或拖拽通道到画布，即可出图；画布支持多页签、网格拆分、十字游标与 A/B 测量，工作区可整体保存为 `.psl` 随时复现。

### 命令行

```bash
plotscope --list-parser      # 查看支持的格式
plotscope --info demo.csv     # 文件解析摘要
plotscope --script x.lua      # headless 跑脚本
plotscope --batch             # 批量处理
plotscope --mcp-bridge        # MCP server（stdio JSON-RPC，供 AI 代理驱动）
```

### 用户文档

系统教程在**[用户手册](docs/manual/README.md)**：[快速上手](docs/manual/getting-started.md) · [数据格式](docs/manual/formats.md) · [绘图交互](docs/manual/plotting.md) · [变换与脚本](docs/manual/operations.md)。

## 开发指南

### 构建代码

依赖 CMake ≥ 3.22、Qt 6（Core/Widgets/Gui/Svg）、C++17 编译器、Rust 工具链：

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

依赖细节、Windows/MSVC 与离线 Qt 部署见[编译与构建](docs/develop/building.md)。

### 代码地图

```text
src/
├── application/   App 基底：shell（组合根）· gui（外壳与视图）· host（运行时编排）· modules（功能模块）
├── libs/          机制库：foundation · dataset · registry · intent · parser · protocol · uischema · fastplot · context · module · recording
├── kernel/        Rust 数据平面：列式数据、LOD、缓存、算子（只经一个 C ABI 头进入）
├── toolbox/       领域工具箱：common · autopilot · automotive · robotics · gnss
└── 3rdparty/      Qt-ADS · SARibbon · Lua 5.4 · MAVLink
```

### 阅读路径

1. [仓库协作要求](AGENTS.md)——现状、红线与协作纪律
2. [分层契约 `src/LAYERS.md`](src/LAYERS.md)——目录定位唯一权威，新增代码先在这里判归属
3. [架构概览](docs/develop/architecture.md)——精简版；深度阅读见[架构总纲](src/ARCHITECTURE.md)
4. [编译与构建](docs/develop/building.md) · [测试](docs/develop/testing.md)

### 扩展开发

- **加一种数据格式**：在 `toolbox/` 对应领域包（或 common 包）实现解析器，`PS_REGISTER_*` 自注册即生效——宿主零改动。
- **加一个领域**：`toolbox/` 建域目录 + 领域枚举加一个值。
- **加一个功能模块**：实现 `IModule` 并静态自注册，装载由宿主完成。
- 协议模板语法见 [psproto 规范](src/libs/protocol/SPEC.md)，UI schema 见 [uischema 语法](docs/develop/uischema-syntax.md)。

## 参与项目

- 开发流程采用 **GitHub Flow**：从 main 切特性分支，PR 合入，保持 main 随时可发布。
- Bug 与需求请建 issue；任务与里程碑看板见仓库 Issues 区。
- 改代码前请先读 [AGENTS.md](AGENTS.md) 与 [`src/LAYERS.md`](src/LAYERS.md)。

## 致谢

- [PlotJuggler](https://github.com/facontidavide/PlotJuggler) 与 [LabPlot](https://labplot.kde.org/)——产品形态与交互设计的参考与借鉴来源。
- 内置三方库：[Qt-Advanced-Docking](https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System)（LGPL-2.1）、[SARibbon](https://github.com/czy1121/SARibbon)（MIT）、[Lua 5.4](https://www.lua.org/)（MIT）、[MAVLink](https://mavlink.io/)（MIT）。

## 许可

本项目采用 **BSD-3-Clause** 许可，见 [LICENSE](LICENSE)。三方组件各自遵循其原始许可。
