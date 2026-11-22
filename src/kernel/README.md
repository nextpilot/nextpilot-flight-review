# PlotScope Kernel

## 定义

Kernel 是**同进程、无 Qt、无格式知识**的共享数据平面。它不按语言定义，当前用 Rust 实现。

它回答：

1. 数据是什么 —— 数据集、通道、时间序列、精度；
2. 怎么算 —— 过滤、四元数、时间对齐、视口 LOD；
3. 数据怎么留 —— 通用二进制缓存、实时流缓冲、流上的 LOD。

它**不负责**：CSV/ULog/OEM7 等格式解析、协议连接、扩展发现、ABI 校验、界面绘制、缓存 key/失效策略。格式与连接属于扩展（toolbox/插件）；key/失效策略属宿主策略（libs/dataset），UI 属于 GUI 层。

## 一屏结构

```text
src/kernel/
  Cargo.toml              workspace：ps-abi / ps-ndf / ps-ops / ps-cache
  include/ps_kernel.h     唯一对外 C ABI；app 只能 include 这一个头
  ps-ndf/                 数据模型：Dataset / Series / 精度 / LOD 算法
  ps-ops/                 纯算子：filters / quat / align
  ps-cache/               数据集二进制缓存：原子写、指纹校验、损坏识别
  ps-abi/                 C ABI 门面：指针、错误码、panic 隔离
```

依赖方向：

```text
ps-abi ──> ps-ndf
   │  └──> ps-ops
   ├─────> ps-cache ──> ps-ndf
```

`ps-ndf` 不依赖其他 crate；`ps-ops` 不依赖 `ps-abi`；`ps-cache` 只依赖 `ps-ndf`，不依赖 App。

## crate 职责

| crate | 负责 | 不负责 |
|---|---|---|
| `ps-ndf` | `Dataset`、`Series`、通道查找/增删、LOD、精度推断 | JSON、C 指针、Qt |
| `ps-ops` | 过滤器、四元数转 RPY、时间对齐 | 数据集生命周期、UI |
| `ps-cache` | 数据集二进制格式、原子写、源文件 size/mtime 指纹校验 | 缓存目录、key 拼接、何时失效 |
| `ps-abi` | C 结构、指针/长度、错误码、panic 隔离、`ps_abi_selftest` | 通道数组本身、格式解析 |

## 对外 ABI

`include/ps_kernel.h` 当前 `PS_ABI_VERSION = 2`，只保留：

- `ps_abi_version / ps_kernel_version / ps_abi_selftest`
- `ps_dataset_new / add_channel / channel_count / channel / free`
- `ps_dataset_apply_filter / preview_filter / apply_quat_rpy / remove_channel`
- `ps_lod_minmax / ps_lod_minmax_many`
- `ps_cache_save_dataset / ps_cache_load_dataset`

约定：

- 不出现 Qt 类型；
- 不出现 `ps_csv_* / ps_ulog_* / ps_oem7_*` 这类格式 API；
- 缓存只接收“数据集 + 路径 + 源文件指纹”，不认 loader、不认选项 JSON；
- 跨边界只传批量数组，不逐点调用；
- `PsChannelInfo` 的 `time/value` 指向内部内存，数据集/流释放或下一次追加后失效。

## 什么不许进 kernel

- CSV/ULog/APBin/BLF/OEM7 解析；
- 扩展 manifest、dlopen、ABI 版本检查（在 `application/host`）；
- Qt、QWidget、Ribbon、Dock；
- 实时 TCP/UDP 连接、设备命令；
- 缓存 key 计算、缓存目录选择、何时删除（在 `libs/dataset`）。

判断方法：**要遍历、变换、派生、缓冲、降采样数据 -> kernel；数据从哪来 -> extension；只在屏幕上存在 -> application/shell。**

## 构建与测试

```bash
cargo test --manifest-path src/kernel/Cargo.toml
cargo build --manifest-path src/kernel/Cargo.toml -p ps-abi --release
```

CMake 在 `src/CMakeLists.txt` 内联导入：构建 `target/release/libps_abi.a` 并包装为 `ps_kernel` INTERFACE 库，头文件目录为 `src/kernel/include`。
