# 编译与构建

## 依赖

- CMake ≥ 3.22
- Qt 6（Core / Widgets / Gui / Svg）
- C++17 编译器
- Rust 工具链（`cargo` 在 `PATH`）
- Python 3（仅部分 `tools/` 脚本需要）

依赖版本（zlib / zstd / lz4 / nanoarrow 的版本、镜像地址与 SHA256）以仓库根 `versions.json` 为唯一来源，`cmake/deps-versions.cmake` 只做解析。升级依赖改 `versions.json`，构建脚本与 CMake 不必同步改。

Qt 可固定到用户目录 `~/.ps-dps`（在仓库与系统之外，不随提交走、不受仓库迁移影响），避免系统包被环境快照回退。

## Linux / 云主机

Qt 固定在 `~/.ps-dps/Qt/`，不依赖系统包——云主机的 apt 包会被环境快照回滚，`~/.ps-dps` 随用户目录留存。首次：

```bash
tools/bootstrap-deps.sh          # Qt 6.2.4 到 ~/.ps-dps/（Lua 内置于 src/3rdparty/lua-5.4，无需下载）
```

之后用系统 Qt 或 `~/.ps-dps` 里的 Qt 配置构建：

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="$HOME/.ps-dps/Qt/6.2.4/gcc_64"
cmake --build build -j4
ctest --test-dir build --output-on-failure        # 无显示器环境自动走 offscreen
```

`~/.ps-dps` 不完整时配置会直接报找不到 Qt6，先跑 `bootstrap-deps.sh`。Lua 由仓库内 `src/3rdparty/lua-5.4` 源码编静态库，不依赖系统 Lua。

系统 Qt（`qt6-base-dev` / `qt6-tools-dev` / `libqt6svg6-dev` / `qt6-l10n-tools`）也能直接 `cmake -S . -B build` 配起来，但不要把它当云主机上的持久方案。

`cargo` 不在默认 PATH 时（如 rustup 安装），构建前 `export PATH="$HOME/.cargo/bin:$PATH"`。

## Windows / MSVC

用 Visual Studio 2022 + Qt 6（按本机安装路径改 `CMAKE_PREFIX_PATH`）：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="<你的Qt路径>/msvc2022_64"
cmake --build build --config Release -j
```

### 运行与测试

Qt 只有 DLL 在 `bin` 里，运行程序和跑 `ctest` 前必须把 Qt `bin` 加进 `PATH`，否则测试会以 `0xc0000135`（找不到 DLL）失败：

```powershell
$env:PATH = "<你的Qt路径>/msvc2022_64/bin;" + $env:PATH
ctest --test-dir build -C Release --output-on-failure
```

Debug / Release 两种配置都要过。测试用 `assert` 驱动，多配置生成器下 Release 会定义 `NDEBUG`；`tests/CMakeLists.txt` 已对 MSVC 统一加 `/UNDEBUG`，保证 Release 跑的是真断言而不是空跑。副作用不要写进 `assert`——Release 会把整段编译掉，测试会静默空跑。

### 已知的 Windows 差异

- 文件删除：Windows 不允许删除仍有打开句柄的文件，读写文件后再 `std::filesystem::remove` 会抛 `filesystem_error`（Linux 无此限制）。

## 构建产物

| 产物 | 路径 |
|---|---|
| CLI（headless） | `build/src/application/app/plotscope` |
| GUI | `build/src/application/gui/plotscope-gui` |
| 测试 | `build/tests/ps_test_*` |

## 运行

```bash
./build/src/application/gui/plotscope-gui
./build/src/application/app/plotscope --info tests/fixture/demo_flight.csv
```

无显示器环境用 Xvfb 做 GUI 冒烟：

```bash
xvfb-run -a -s '-screen 0 1600x1000x24' \
  env QT_QPA_PLATFORM=xcb ./build/src/application/gui/plotscope-gui
```

## 设置存储

`App` 构造时接收 `settingsPath`：为空时设置只存内存（进程退出即失）；传入 JSON 路径则读写该文件。GUI 内可通过「导出设置」把当前设置导出为 `plotscope-settings.json`。CLI 的 `--record` / `--run` 等场景用 `data.workspace.save` 保存工作区配方。
