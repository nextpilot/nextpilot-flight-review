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
    ui/                       Shell、画布、数据输入、脚本、变换、视频
    resources/                图标资源、Lua 脚本

  extensions/
    builtin/                  随包发布的业务组件
      gnss/                   NMEA / UBX / OEM7 数据源
      custom_protocol/        自定义协议加载、编辑、编解码；protocol/ 为其协议模型
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
| 窗口、Ribbon、Dock、对话框 | `base/ui/` | 只在屏幕上存在 |
| 具体格式解析、协议、设备数据源 | `extensions/` | 决定数据从哪来、怎么解释 |
| 用户 Lua 脚本与图标 | `base/resources/` | 运行期资源，不进 host 头文件 |

内置组件与外部动态扩展统一通过 `ExtensionRegistry::addExtension()` 注册；内置组件自己静态注册，外部 `.so` 由 `ExtensionRuntime` 装载后交给 `DynamicExtension` 适配。
