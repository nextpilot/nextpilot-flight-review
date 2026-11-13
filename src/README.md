# src 目录

`src` 只按“职责”分目录，不按语言分。Rust 目前只出现在 kernel，但 kernel 的身份不是“Rust 目录”，而是“与 Qt 无关的抽象业务逻辑”。

```text
src/
  app/          Qt 实现 + 宿主基础设施
    shell/      主窗口、Ribbon、Dock
    plot/       画布、坐标轴、图例
    loader/     loader 注册与通用选项/对话框
    source/     实时源注册与配置/调试
    transform/  变换对话框
    script/     Lua 绑定
    plugin_host/ 插件发现、装载、ABI 校验、生命周期
    cache/       解析结果缓存、参数缓存（不是内核数据模型）

  kernel/       与 Qt 无关的抽象业务逻辑（当前 Rust 实现）
    include/    ps_kernel.h，唯一对外 C ABI
    ps-ndf/     数据模型：Dataset / Series / LOD / 精度
    ps-ops/     通用算子：filter / quat / align
    ps-abi/     C ABI 门面：只做指针、错误码、panic 隔离

  plugin/       插件（官方随包发布）
    include/    ps_plugin_api.h，插件契约
    data_loader/ CSV / ULog / APBin / ASC / BLF
    data_source/ 仿真 / MAVLink / OEM7

  scripts/      用户可改的 Lua 脚本与算子 schema
  3rdparty/     第三方源码
```

## 归属规则

| 东西 | 放哪 | 判据 |
|---|---|---|
| 数据模型、通用算子、LOD | `kernel/` | 要遍历/变换/降采样，且不碰 Qt、不做 I/O |
| 插件发现、ABI 校验、装载 | `app/plugin_host/` | 管宿主生命周期，不负责计算 |
| 解析缓存、参数缓存 | `app/cache/` | 管“上次解析结果”，不持有内存数据模型 |
| 文件/协议解析、配置 schema | `plugin/` | 决定数据从哪来 |
| 窗口、Ribbon、Dock、对话框 | `app/` | 只在屏幕上存在 |
| 用户 Lua 脚本 | `scripts/` | 可热改、可重载，不编进二进制 |

`kernel` 不允许出现 `QWidget`、文件路径、TCP/UDP、CSV/OEM7 字段名。`app` 不允许绕过 `ps_kernel.h` 直接碰 kernel 内部类型。

## Rust 的位置

Rust 不是某一层的身份，而是实现手段：

- `kernel/` 里的 Rust 负责共享数据模型、批量算子、LOD；
- `plugin/` 里的 Rust 负责格式解析、协议、I/O，但只能通过 `ps_plugin_api.h` 和宿主函数写数据；
- 插件不得静态链接第二份 kernel，避免两套 allocator 和两套数据模型。
