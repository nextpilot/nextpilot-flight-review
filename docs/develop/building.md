# 编译与构建

## 依赖

- CMake ≥ 3.24（3.22.x 处理 `URL_HASH` 会失败，版本门槛见 `versions.json`）
- Qt 6（Core / Widgets / Gui / Svg）
- C++17 编译器
- Rust 工具链（`cargo` 在 `PATH`）
- Python 3（仅部分 `tools/` 脚本需要）

`versions.json` 分两段，每个条目把版本、镜像和附加配置写在一起：

- `toolchains`（gcc / cmake / ninja / ccache / python / rust / qt / apt）：由 `tools/ubuntu-setup.sh` 体检与获取。`version` 用带运算符的表达式表达要求（`">=3.24"` 最低版本、`"=6.2.4"` 精确版本）；`mirrors` 是该工具的下载镜像数组。
- `packages`（zlib / zstd / lz4 / nanoarrow）：构建期由 `FetchContent` 拉源码。每个包自带 `mirrors` 数组，元素为 `{url, sha256}`，哈希与 URL 绑定。`cmake/deps-versions.cmake` 只解析这一段。

升级依赖、换镜像都只改 `versions.json`，脚本与 CMake 不必同步改。

Qt 可固定到用户目录 `~/.ps-deps`（在仓库与系统之外，不随提交走、不受仓库迁移影响），避免系统包被环境快照回退。

## Linux / 云主机

一条命令装齐工具链、Qt、Rust 与 Python venv，版本全部取自仓库根 `versions.json`：

```bash
tools/ubuntu-setup.sh --check     # 体检：每项显示 实测 / 要求 / 来源(system|managed)
tools/ubuntu-setup.sh --install   # 补齐缺项并写入 shell 变量
```

装到 `~/.ps-deps`（仓库外、随用户目录留存，不受 apt 快照回滚影响）。系统自带且版本达标的工具不重复安装，只有不达标的才落进统一目录。

**工具下载镜像**：每个会联网的工具（python/pip、rust、qt、apt）在自己的 `toolchains.<工具>.mirrors` 给一组 URL，国内在前、官方兜底；安装时**按顺序逐个轮询，第一个可达的即用**——避免国内网络下官方源超时卡死。

```json
"python": {
  "version": ">=3.10",
  "configuration": "tools/requirements.txt",
  "mirrors": [ "https://mirrors.aliyun.com/pypi/simple/", "https://pypi.org/simple/" ]
}
```

- 轮询用 6s 超时探测，某个源慢或不可达自动跳下一个；全部不可达时用第一个并报真实错误。
- 换源/调优先级只需改 `versions.json` 的数组顺序，脚本无需改动；某工具未配 `mirrors` 时沿用原行为。
- pip 若已在 `pip.conf` 显式配置了 index，脚本不覆盖用户配置；apt 仅对 Ubuntu 生成临时源，不改系统 `/etc/apt`。

**源码包镜像**：`packages.<pkg>.mirrors` 每个元素自带 `{url, sha256}`（国内 gitee 归档在前、GitHub 官方 tar.gz 兜底）。由于 gitee 的 zip 与 GitHub 的 tar.gz 内容不同、哈希不同，CMake 的 `URL_HASH` 无法共用，构建期 `deps-versions.cmake` 用 for 循环逐个探测下载并按各自哈希校验，选第一个通过的交给 `FetchContent`（下载结果缓存在 build 目录，重复 configure 不再联网）；系统已有该库时完全不触发网络。换源/加源只改 JSON 数组。

之后构建不需要任何 `-D` 参数：

```bash
cmake --preset default                          # Release
cmake --build --preset default -j"${PS_JOBS:-$(nproc)}"
ctest --test-dir build --output-on-failure      # 无显示器环境自动走 offscreen

cmake --preset fast                             # 关掉 37 个测试目标，改代码只编主程序
cmake --preset debug                            # Debug + 断言，落在 build-debug/
```

并行度按本机核数自适应（`nproc`），不写死 `-j2`——小内存机器（如 2 核/2 GB 的 VM）临时收窄即可：

```bash
PS_JOBS=2 cmake --build --preset default
```

> **生成器是 Ninja**（`CMakePresets.json` 里 `"generator": "Ninja"`）。从 Unix
> Makefiles 切换过来时，旧的 `build/`、`build-debug/` 必须删掉重建：CMake 会拒绝
> 在生成器不同的目录上 configure（报 `Generator ... does not match the generator
> used previously`）。缓存本身仍然有效，Ninja 下 ccache 增量命中实测 100%，
> 数据与机制见 `build-performance.md` §9。

`~/.ps-deps` 不完整时 configure 会报找不到 Qt6，先跑 `tools/ubuntu-setup.sh --install --qt`。Lua 由仓库内 `src/3rdparty/lua-5.4` 源码编静态库，不依赖系统 Lua。

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
