# src 目录

`src` 按职责分层：`base` 是 PlotScope 底座，只提供 API、框架与宿主能力，不含具体业务；`extensions` 承载具体业务能力。

```text
src/
  base/                       底座：内核、宿主扩展层、UI、资源
    main.cpp                  入口；只认扩展注册表，不 include 具体组件
    host/
      extension/              扩展契约、注册表、动态装载、宿主 API
      services/
        cache/                数据集缓存
        live/                 实时会话与录制
    kernel/                   与 Qt 无关的抽象逻辑（Rust，ps_kernel.h）
    ui/                       Shell、主题、通用数据输入/扩展对话框、基础画布、脚本面板
    resources/                图标资源、Lua 脚本

  extensions/
    builtin/                  随包发布的业务组件
      data_sources/           内建文件/实时源聚合
      gnss/                   NMEA / UBX / OEM7 数据源
      custom_protocol/        自定义协议加载、编辑、编解码；protocol/ 为其协议模型
      video/                  视频同步面板
      device_debug/           源设备调试终端
      transform/              绘图前数据变换
      script/                 Lua/Python 脚本引擎
    dynamic/                  外部动态扩展（.so / .dll）
      api/                    ps_extension_api.h，扩展 ABI
      sdk/                    C++ / Rust 扩展 SDK
      data_loader/            CSV / ULog / APBin / ASC / BLF / MCAP 等
      data_source/            仿真 / MAVLink

  3rdparty/                   第三方源码
```

## 归属规则

| 东西 | 放哪 | 判据 |
|---|---|---|
| 数据模型、通用算子、LOD、缓存/流机制 | `base/kernel/` | 不碰 Qt、不做 I/O |
| 扩展契约、注册表、动态装载、宿主 API | `base/host/extension/` | 只做宿主生命周期与能力暴露 |
| 缓存策略、实时会话与录制 | `base/host/services/` | 宿主侧服务，不是业务格式 |
| 窗口、Ribbon、Dock、通用对话框、基础画布 | `base/ui/` | 宿主骨架与通用 UI，不认识具体业务 |
| 设备调试、视频同步、数据变换、脚本引擎 | `extensions/builtin/` | 有业务语义或外部工具依赖 |
| 具体格式解析、协议、设备数据源 | `extensions/` | 决定数据从哪来、怎么解释 |
| 用户 Lua 脚本与图标 | `base/resources/` | 运行期资源，不进 host 头文件 |

内置组件与外部动态扩展统一通过 `ExtensionRegistry::addExtension()` 注册；内置组件自己静态注册，外部 `.so` 由 `ExtensionRuntime` 装载后交给 `DynamicExtension` 适配。

## 组件可裁剪

内置业务组件默认构建，可按需关闭：

```bash
cmake -S . -B build -DPS_BUILD_GNSS=OFF -DPS_BUILD_CUSTOM_PROTOCOL=OFF
```

`base` 不 include 任何组件头文件；组件通过注册表注入 Ribbon、Dock、DataSource、对话框和脚本运行器。业务 UI 以 `DialogContribution` / `DockContribution` / `ScriptRunnerContribution` 注入，base 只按 id 创建，不认识具体类型。
