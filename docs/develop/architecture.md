# 架构概览

本文是当前架构的精简速览：结构、核心抽象、数据流、扩展装配。设计取舍的完整论证见 [`src/ARCHITECTURE.md`](../../src/ARCHITECTURE.md)，目录定位与依赖规则见 [`src/LAYERS.md`](../../src/LAYERS.md)。

## 分层结构

```text
src/
├── application/   App 基底（不可复用，绑死产品形态）
│   ├── app/           组合根：App 装配（CLI / GUI / MCP 桥 / 诊断）
│   ├── gui/           壳：主窗、绑定器、通用面板、主题（链接关：零模块）
│   ├── bare/          裸壳独立产物 ps_shell_bare（零模块第二产品）
│   ├── tools/         装配层工具 / 基准产物（可链模块与工具箱）
│   ├── host/          宿主服务：意图处理、数据面、录制、装配
│   └── modules/       功能模块：data / plot / script / protocol / analysis / settings / report
├── libs/          机制库（可复用）
│   ├── context/       AppContext 运行态 + SettingsStore
│   ├── dataset/       数据集 / SourceHub
│   ├── parser/        解析器 SPI（ps_parser_spi.h）
│   ├── registry/      贡献注册（Pack/UiAction/Panel/Report…）
│   ├── intent/        意图通道
│   ├── uischema/      属性 schema 契约
│   ├── recording/     录制：格式 × 后端
│   ├── protocol/      通用帧协议解析
│   └── fastplot/      2D 绘制内核（Qt 之上独立）
├── kernel/        Rust ABI 内核（列式数据、LOD、变换、quantiles）
├── toolbox/       工具箱领域包
│   ├── common/        CSV、自定义协议
│   ├── autopilot/     飞控（ulog / apbin）
│   ├── automotive/    汽车（mdf / blf）
│   ├── robotics/      机器人（rosbag2）
│   └── gnss/          卫导
└── 3rdparty/      第三方（ADS、SARibbon、Lua 5.4、mavlink 头）
```

依赖规则由构建期守卫强制（`cmake/LayerContractGuard.cmake`、`cmake/LibBoundaryGuard.cmake`，违规直接构建失败）：

- **kernel / libs / toolbox 禁止 include `application/` 下的任何头文件**——机制不反向依赖基底。
- libs 内禁止逃逸 include、禁止单例、禁止链接上层 target。
- **壳 `ps_gui` 链接关**：只链 libs 层 + Qt + fastplot，不链任何 `ps_module_*` / `ps_toolbox_*` / `ps_parser_*`；模块与工具箱只由装配根（exe）链接。护栏：`cmake/ShellLinkGuard.cmake`。
- 模块之间互不链接，只经 host 注册与意图协作。

## 核心抽象

| 抽象           | 定义                                                                         | 关键约定                                                       |
| -------------- | ---------------------------------------------------------------------------- | -------------------------------------------------------------- |
| **DataSource** | 来源 + 解析绑定 + 数据集的三段聚合                                           | 源 id 与数据集 id 是两套 id 空间                               |
| **Parser**     | 纯转换：字节流 → Dataset，不持 UI 不持状态                                   | 两类：格式解析器（内置/插件）与协议解析器（schema 驱动），不混 |
| **Dataset**    | 唯一数据形态（列式，Rust kernel 持有）                                       | 原始数据零污染——变换/衍生都产派生序列，不写回                  |
| **Intent**     | `域.动作` 命名的意图通道（`data.load`、`plot.layout.*`、`toolbox.install`…） | UI 不直碰数据层，动作走意图                                    |
| **Pack**       | 工具箱装载/卸载的原子单位，聚合七类贡献                                      | 领域知识只在 toolbox，宿主不侵入                               |
| **AppContext** | 进程级运行态唯一持有者，其余层构造注入                                       | 无全局单例、无服务定位器                                       |

## 数据流

```text
文件/实时源 → DataSource（前端） → Parser（扇出） → Dataset（Rust kernel）
                                                  ↓
        GUI View / Analysis / Report ← 意图通道 ← AppContext
```

- **摄取**：单前端建源，多解析器扇出，落 Dataset；解析器在 toolbox 包内经 `PS_REGISTER_*` 自注册。
- **消费**：View 通过 LOD 三元组（Rust 侧算好 min/max 金字塔）交给 fastplot 绘制。
- **衍生**：变换/脚本产派生序列走 `AppContext::derivedSeriesReady` 只读叠加，不写回数据集。
- **录制**：`libs/recording` 是「格式 × 后端」正交组合（MCAP/PSRAW1/CSV × 文件/UDP），两侧独立注册按 id 组合。

## 扩展装配

三条通道，按形态选：

| 通道         | 载体                                                | 例子                               |
| ------------ | --------------------------------------------------- | ---------------------------------- |
| 工具箱数据包 | zip 包（manifest + packs/*.json），运行时安装/卸载  | PX4 调参包、预置分析入口           |
| 内置领域包   | `src/toolbox/<域>/` 源码，`PS_REGISTER_PACK`        | ulog/apbin/mdf4/blf/rosbag2 解析器 |
| 解析器插件   | `psplug_*.so`（dlopen，C ABI `ps_plugin_abi` 校验） | 第三方格式解析器                   |

新增领域 = toolbox 加一个域目录 + 枚举加一个值，宿主代码零改动；插件失败只隔离自身，不影响内置格式。

## GUI 装配

- 外壳只留一个绑定器 `ShellBinder`（Ribbon 绑定 + dock 绑定），不设 DockManager/RibbonManager 中转类。
- UI 顺序是纯数据契约：所有槽/组/动作带 `order` 字段，与模块装载顺序解耦。
- 视图自注册（`PS_REGISTER_PANEL`），领域自带面板形态归 toolbox 的 `gui/`。
- 主题走 token 化配色（`ThemeManager`），未知 token 返回洋红 `#FF00FF`，便于暴露误用。
- 产品身份唯一来源 `registry::ProductSpec`（id / 显示名 / 默认 Region 布局 / 模块清单），由装配层注入壳；壳内无产品常量、无产品分支。完整产品与裸壳 `ps_shell_bare`（零模块第二产品，`PS_BUILD_BARE` 默认 ON）共用同一份 `ps_gui`。

## 构建入口

顶层 `CMakeLists.txt` 是薄壳：`project(PlotScope)` + `enable_testing()` + `add_subdirectory(src)` + `add_subdirectory(tests)` + 分层守卫。`src/CMakeLists.txt` 是构建事实源。详见[编译与构建](building.md)。
