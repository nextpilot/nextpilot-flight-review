# PlotScope Kernel

## 定义

Kernel 是**同进程、无 Qt、无 I/O、无格式知识**的抽象业务内核，当前用 Rust 实现。

它只回答三个问题：

1. 数据是什么 —— 数据集、通道、时间序列、精度；
2. 怎么算 —— 过滤、四元数、时间对齐等通用算子；
3. 怎么切片 —— 视口 LOD（min/max 桶）。

它**不负责**：文件解析、协议解码、插件发现、ABI 校验、实时连接、界面绘制。这些分别属于 loader/source 插件、runtime 和 app。

## 一屏结构

```text
src/kernel/
  Cargo.toml              workspace（只含 ps-abi / ps-ndf / ps-ops）
  include/ps_kernel.h     唯一对外 C ABI；app 只能 include 这一个头
  ps-ndf/                 唯一数据模型：Dataset / Series / LOD / 精度
  ps-ops/                 纯算子：filters / quat / align
  ps-abi/                 C ABI 门面：把 ndf + ops 映射成 C，不存业务模型
```

依赖方向固定为：

```text
ps-abi  ──> ps-ndf
   │
   └──────> ps-ops
```

`ps-ndf` 不依赖其他 crate；`ps-ops` 不依赖 `ps-abi`；`ps-abi` 不允许反过来被数据模型依赖。

## crate 职责

| crate | 负责 | 不负责 |
|---|---|---|
| `ps-ndf` | `Dataset`、`Series`、通道查找/增删、LOD、精度推断 | JSON、C 指针、Qt |
| `ps-ops` | 过滤器、四元数转 RPY、时间对齐 | 数据集生命周期、UI |
| `ps-abi` | C 结构、指针/长度、错误码、panic 隔离、`ps_abi_selftest` | 通道数组本身、格式解析 |

## 对外 ABI

`include/ps_kernel.h` 只保留：

- `ps_abi_version / ps_kernel_version / ps_abi_selftest`
- `ps_dataset_new / add_channel / channel_count / channel / free`
- `ps_dataset_apply_filter / preview_filter / apply_quat_rpy / remove_channel`
- `ps_lod_minmax`
- `ps_lod_minmax_many`（一次处理多通道；通道数 >= 8 时内核内部并行）

约定：

- 不出现 Qt 类型；
- 不出现 `ps_csv_* / ps_ulog_* / ps_oem7_*` 这类格式 API；
- 跨边界只传批量数组，不逐点调用；
- 绘图热路径优先走 `ps_lod_minmax_many`，UI 不逐通道跨 FFI；
- `PsChannelInfo` 的 `time/value` 指向数据集内存，数据集释放后立即失效；
- 内核 panic 不得穿过 FFI，统一转错误码。

## 什么不许进 kernel

- CSV/ULog/APBin/BLF/OEM7 解析；
- 插件 manifest、dlopen、ABI 版本检查（在 `src/app/runtime`）；
- Qt、QWidget、Ribbon、Dock；
- 实时 TCP/UDP 连接；
- 文件路径查找、缓存 key 计算（缓存策略在 `app/cache`；将来若做通用缓存存储，只接收不透明 key）。

判断方法：**要遍历、变换、派生、降采样数据 -> kernel；数据从哪来 -> 插件；只在屏幕上存在 -> app。**

## 构建与测试

```bash
cargo test --manifest-path src/kernel/Cargo.toml
cargo build --manifest-path src/kernel/Cargo.toml -p ps-abi --release
```

CMake 通过 `cmake/Rust.cmake` 的 `ps_import_kernel()` 链接 `target/release/libps_abi.a`，头文件目录为 `src/kernel/include`。
