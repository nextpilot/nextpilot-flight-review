# src 目录

`src` 按功能模块组织：`base` 是最小 app + 最小绘图 app；`extensions` 在其上叠加行业/设备业务。

```text
src/
  base/                       最小 app + 最小绘图 app
    main.cpp                  入口；组装 shell 与内置模块
    host/
      extension/              扩展契约、注册表、动态装载、宿主 API
      services/
        cache/                数据集缓存
        live/                 实时会话与录制
    kernel/                   与 Qt 无关的抽象逻辑（Rust，ps_kernel.h）
    modules/
      runtime/                脚本/扩展运行契约
      shell/                  MainWindow、Ribbon/Dock 容器、主题、扩展对话框
      data_source/            数据源 Dock、数据输入对话框、数据 Ribbon
      plot/                   CanvasWidget、绘图 Ribbon、页面/图型
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
| 主窗口、Ribbon/Dock 容器、主题 | `base/modules/shell/` | app 骨架，只做组装 |
| 数据源 Dock、输入对话框、数据 Ribbon | `base/modules/data_source/` | 数据进入与源管理 |
| 画布、绘图 Ribbon、页面/图型 | `base/modules/plot/` | 绘图工作区 |
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
