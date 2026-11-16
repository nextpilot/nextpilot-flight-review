# 编译与构建

## 依赖

- CMake ≥ 3.22
- Qt 6 Widgets
- C++17 编译器
- Rust 工具链（`cargo` 在 `PATH`，或配置 `-DPS_CARGO=<path>`）
- zlib / zstd / lz4（缺失时 CMake 从 Gitee 镜像拉源码静态构建）

仓库可以把 Qt 与 Lua 固定到 `deps/`（已 gitignore，不随提交走），避免系统包被环境快照回退。

## Linux / 云主机

系统 Qt 直接可用时（Ubuntu 22.04 的 `qt6-base-dev` / `qt6-tools-dev` / `libqt6svg6-dev` / `qt6-l10n-tools`，加系统 zlib/zstd/lz4），走 `linux` 预设，不需要 `deps/`：

```bash
cmake --preset linux            # Debug，产物 build/
cmake --build --preset linux-debug
ctest --preset linux            # 自动设 QT_QPA_PLATFORM=offscreen
```

`linux` 预设会把 `~/.cargo/bin` 加进 `PATH`，rustup 装的 cargo 不用手工 export。Release 用 `linux-release` 预设（产物 `build-release/`）。

没有系统 Qt 时需要自建依赖：

```bash
tools/bootstrap-deps.sh   # 下载 Qt 6.2.4 + 编译 Lua 5.4.4 到 deps/
tools/build-local.sh      # 只用 deps/ 的 Qt/Lua 构建并跑 ctest
```

手动构建：

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="$PWD/deps/Qt/6.2.4/gcc_64"
cmake --build build -j4
LD_LIBRARY_PATH="$PWD/deps/Qt/6.2.4/gcc_64/lib" ctest --test-dir build --output-on-failure
./build/src/application/plotscope_app
```

## Windows / MSVC

`CMakePresets.json` 提供 `msvc` 配置预设，默认指向 `D:/program/Qt/6.12.0/msvc2022_64`。按本机 Qt 路径改 `CMAKE_PREFIX_PATH`：

```powershell
cmake --preset msvc
cmake --build --preset msvc-release
```

或直接：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="D:/program/Qt/6.12.0/msvc2022_64"
cmake --build build --config Release -j
```

## 只构建 Rust 内核

```bash
cmake -S . -B build -DPS_BUILD_GUI=OFF
cmake --build build -j
```

## 常用 CMake 选项

| 选项 | 默认 | 说明 |
|---|---|---|
| `PS_BUILD_GUI` | `ON` | 构建 Qt Widgets 主程序 |
| `PS_BUILD_TESTS` | `ON` | 构建测试 |
| `PS_ENABLE_LUA` | `ON` | 启用 Lua 5.4 脚本 |
| `PS_BUILD_GNSS` | `ON` | 构建 GNSS 内置组件 |
| `PS_BUILD_CUSTOM_PROTOCOL` | `ON` | 构建自定义协议内置组件 |
| `PS_EXTENSION_DIR` | `src/extensions/dynamic` | 动态扩展源码目录 |
| `PS_EXTENSION_OUTPUT_DIR` | `build/src/application/extensions` | 开发树扩展产物目录 |
| `PS_DEPS_ROOT` | 空 | 指向已安装依赖的 `include/` + `lib/` |

## 运行

```bash
./build/src/application/plotscope_app
./build/src/application/plotscope_app tests/fixture/demo_flight.csv
```

无显示器环境用 Xvfb 做 GUI 冒烟：

```bash
xvfb-run -a -s '-screen 0 1600x1000x24' \
  env LD_LIBRARY_PATH="$PWD/deps/Qt/6.2.4/gcc_64/lib" QT_QPA_PLATFORM=xcb \
  ./build/src/application/plotscope_app tests/fixture/demo_flight.csv
```

配置目录默认在 `~/.config/NextPilot/PlotScope`，可用 `XDG_CONFIG_HOME` 指向临时目录做隔离测试。
