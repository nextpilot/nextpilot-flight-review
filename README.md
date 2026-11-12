# PlotScope

面向飞控 / 嵌入式 / 车辆研发分析的通用绘图与数据分析工具。对标 PlotJuggler + JCom，
主打**千万点级数据、5 kHz × 200 通道**的实时可视化与脚本算子。

设计文档：`docs/PlotScope-架构设计-定稿-v1.0.md`（决策 D1~D16 在此）
UI 原型：`docs/prototype/PlotScope-工作区原型.html`

## 目录

```
cmake/            CMake 模块（Rust.cmake / Warnings / Version / Install / FindJKQTPlotter）
src/kernel/       Rust 内核，Cargo workspace 根，产出 ps-abi（C ABI）
src/app/          Qt 6 Widgets 主程序，只 include ps_kernel.h
src/plugin/       官方 Loader（按家族一个目录，带 manifest.json）
src/function/     Lua 算子与批处理脚本
src/3dparty/      锁版本三方库（JKQTPlotter / lua-5.4）
tool/             ps-plugin-check / ps-bench / ps-pack
test/             unit / kernel / golden / perf / fixture
docs/             架构文档 + 单文件原型（不进安装包）
```

## 构建

依赖：CMake ≥ 3.22、Qt 6 Widgets、Rust（rustup，cargo 在 PATH 或 `-DPS_CARGO=`）。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/src/app/plotscope_app
```

无 Qt 环境只构建内核：`cmake -S . -B build -DPS_BUILD_GUI=OFF`。

## 当前状态（M0 骨架）

- 内核：`ps-abi`（版本 / 自检）、`ps-ndf`（序列 + 抽稀）、`ps-ops`（scale/add/moving_avg）、
  `ps-align`（最近邻 / 线性）、`ps-parse`（格式嗅探）、`ps-script`（预算占位），14 个单元测试通过。
- 数据面：`ps_csv_load` 读 CSV 成数据集（`, ; tab` 三种分隔、ns/us/ms/s 时间识别），
  `ps_dataset_channel*` 暴露通道指针，`ps_lod_minmax` 在 Rust 侧按像素做 min/max 抽稀。
- 主程序：Ribbon 七页签 + 四可停靠区 + 页签式绘图区；自绘画布支持滚轮缩 Y（锚指针）、拖动平移、
  双击复位、十字光标；命令行带 CSV 直接加载（`plotscope_app flight.csv`）。
- 未接入：JKQTPlotter、Lua 5.4、ULog/DFLog 等真实 Loader、NDF 落盘、多 Y 轴、仪表自绘。

## 许可

BSD-3-Clause。三方库许可在 `docs/` 与安装包 `third-party/` 中登记。
