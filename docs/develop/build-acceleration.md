# 构建加速：依赖镜像与编译缓存

## 为什么要镜像

本工程的源码依赖（zlib / zstd / lz4 / nanoarrow / sqlite3）在缺失时由 CMake `FetchContent` 拉源码编译。
直接从上游（GitHub release / 官方站点）下载在国内网络环境下经常超时或拿到 0 字节文件，
表现为「配置阶段卡住」或「下载成功但解压失败」。

解决办法是把这些依赖镜像到 Gitee 的 `nphelper` 组织下，构建时从 Gitee 拉包：

| 依赖 | 镜像仓库 | 拉取形态 |
|---|---|---|
| zlib | `gitee.com/nphelper/zlib` | `repository/archive/<tag>.zip` |
| zstd | `gitee.com/nphelper/zstd` | 同上 |
| lz4 | `gitee.com/nphelper/lz4` | 同上 |
| nanoarrow | `gitee.com/nphelper/nanoarrow` | 同上 |

## 镜像仓库的建法

以 nanoarrow 为例，镜像 = 上游仓库的 tag 归档，不含任何本地改动：

```bash
# 1. 按 tag 浅克隆（不要 --depth，否则 tag 可能取不到）
git clone --depth 1 --branch apache-arrow-nanoarrow-0.9.0 \
    https://github.com/apache/arrow-nanoarrow.git nanoarrow

cd nanoarrow
git checkout -b master apache-arrow-nanoarrow-0.9.0
git branch -a && git tag          # 确认 tag 已被带上来

# 2. 在 gitee.com/nphelper 下新建同名空仓库（public，默认分支 master）

# 3. 推分支与 tag（tag 必须推，archive/<tag>.zip 依赖它）
git remote add gitee https://gitee.com/nphelper/nanoarrow.git
git push gitee master
git push gitee --tags
```

要点：

- **tag 必须推上去**。`repository/archive/<tag>.zip` 靠 tag 定位，只有分支推上去会得到 404。
- **默认分支用 `master`**，与 zlib/zstd/lz4 现状一致。
- 镜像内容与上游 release tarball 的差异只有 `python/subprojects/arrow-nanoarrow`（Python 打包子项目，
  git 里是 submodule、release 包里是完整目录）。C/C++ 源码两者逐字节相同，CMake 构建路径不受影响。

## nanoarrow 的下载配置

`cmake/Dependencies.cmake` 里 nanoarrow 有两个候选 URL，CMake 依次尝试、第一个成功的即被采用：

```cmake
set(PS_DEPS_NANOARROW_URL
    "https://gitee.com/nphelper/nanoarrow/repository/archive/apache-arrow-nanoarrow-0.9.0.zip"
    "https://github.com/apache/arrow-nanoarrow/releases/download/apache-arrow-nanoarrow-0.9.0/apache-arrow-nanoarrow-0.9.0.tar.gz"
    CACHE STRING "nanoarrow 源码包")
```

第二个 URL 是上游 GitHub release，作为镜像未同步时的兜底。

### 哈希校验的坑

`PS_DEPS_NANOARROW_SHA256` **默认留空**（与 zlib/zstd/lz4 一致，那三个本来就没有哈希校验）。原因是：

- 多个候选 URL 共用同一个 `URL_HASH`，CMake 会对**实际下载到的那个文件**校验；
- Gitee 归档 zip 是服务端重新打包的，与上游 release tar.gz 哈希不同；
- 填上游 tar.gz 的哈希 → 走镜像时下载成功但校验失败，构建中断。

镜像同步完成后，若要开启校验，需填入 **Gitee zip 的实际哈希**，且必须保证镜像与该哈希一一对应；
否则宁可留空。`FetchContent_Declare` 侧已改为条件追加 `URL_HASH`，留空时不会生成校验参数。

## 编译缓存

`PS_DEPS_FETCH` 之外还有两层加速：

| 手段 | 开关 / 入口 | 说明 |
|---|---|---|
| ccache | `PS_USE_CCACHE`（默认 ON） | 根 `CMakeLists.txt` 裸 `find_program(ccache)`，命中即设 `CMAKE_CXX_COMPILER_LAUNCHER`；探测不到就静默直连 |
| 测试目标裁剪 | `PS_BUILD_TESTS`（默认 ON） | 关闭后不编测试目标，显著缩短日常迭代构建；此时 ctest 无用例可跑，属预期 |

ccache 本身由 `tools/ubuntu-setup.sh --install --toolchain` 安装（自包含目录落
`~/.ps-deps/toolchain/ccache/`）。因为 `CMakeLists.txt` 里的探测靠 shell 的 `PATH`，
换机器后重跑一次脚本即可，不需要改仓库配置。

### 配置分三层（按"是否与路径绑定"划分）

ccache 的配置**不要堆在 shell 环境变量里**。环境变量优先级高于 `ccache.conf`，
一旦 `~/.bashrc` 里 export 了策略项，就只有 source 过脚本的终端生效，IDE / CI /
直接调 `ccache` 的场景会用另一套值。按下面三层放，所有入口读同一份：

| 配置项 | 放哪里 | 理由 |
|---|---|---|
| `CCACHE_DIR` | `.bashrc` / `--print-env`（脚本写） | 决定缓存物理位置；也让手动 `ccache -s` 读到同一份缓存 |
| `max_size` / `depend_mode` / `hash_dir` / `sloppiness` | `$CCACHE_DIR/ccache.conf`（脚本生成） | 纯策略、与路径无关；持久、团队一致、CI 也认 |
| `CCACHE_BASEDIR` | `CMakePresets.json` 的 `environment`（`${sourceDir}`） | 与仓库绝对路径绑定，必须随仓库版本化；IDE / CI / `cmake --preset` 都读 |

脚本生成的 `ccache.conf`（`--install` 和 `--warm-cache` 都会确保它存在）：

```ini
max_size = 5G
depend_mode = true    # 用编译器 -MD 依赖信息判命中，跳过完整预处理
hash_dir = false      # 工作目录不进哈希（本项目不用 __FILE__，无正确性风险）
sloppiness = pch_defines,time_macros
```

**为什么 `depend_mode` 值得开**：8 文件最小工程实测，二次全命中从 90ms 降到
65ms（命中判断不再跑完整预处理）；GCC 支持，CMake 默认给 CXX 加 `-MD -MT`，
无需额外参数。

**为什么 `base_dir` 不进 conf**：它与 checkout 的绝对路径绑定，写死在
`$CCACHE_DIR/ccache.conf`（本地、跨仓库共享）会串味。放进 preset 用 `${sourceDir}`
才能随仓库版本化。实测：两份不同 checkout 路径、各自 `CCACHE_BASEDIR=自身根`，
同一份源码 **8 hit / 0 miss**——换机器、换目录仍能命中。

**一个要澄清的误区**：`hash_dir=false` / `CCACHE_NOHASHDIR` **不能**让 Release 与
Debug 共享缓存。实测 `-O2` 编完再 `-O2 -g`，开不开 `NOHASHDIR` 都是 8 miss——
两者编译参数不同，哈希本就不同。切 preset 之所以"从零编"，主因是 `-O2`/`-g`
参数差异，不是工作目录。`hash_dir` 只在"同参数、仅工作目录不同"时有用
（实测 `-g` 跨 CWD：开则 8 miss，关则 8 hit）。

### 预热缓存：--warm-cache

ccache 的收益要等缓存里真有过东西才兑现。换机器、清过缓存目录、CI 冷 runner
都是空缓存，此时第一次全量构建仍然要跑满全程。

```bash
tools/ubuntu-setup.sh --warm-cache          # 跑一次全量构建把缓存填满
tools/ubuntu-setup.sh --warm-cache --force  # 缓存已达标也强制重跑
tools/ubuntu-setup.sh --install --warm-cache  # 装环境时顺手预热
```

不预热也不会坏，只是第一次构建仍是冷构建。脚本用 `ccache --print-stats` 的
`cache_size_kibibyte` 判断缓存体积，阈值在 `versions.json` 的
`toolchains.ccache.warm_min_mb`（默认 12MB，本仓库一次全量构建约产生 16.5MB）。

判定还要求**预热标记与当前源码版本一致**
（`~/.ps-deps/state/ccache-warm.default.stamp` 与 `.debug.stamp`）。
只看体积不够：缓存目录是全局共享的，别人留下的几百 MB 缓存也会让体积达标，
但里面未必有本仓库的条目——那种情况下日常构建照样 miss，提示就失效了。
标记绑定 git HEAD + 工作区差异 + 未跟踪文件列表，任何一处改动都会让提示重新出现。

预热必须遵守三条，否则等于白烧几分钟（详见"配置分三层"与下方实测）：

1. **用 preset 本身** configure+build（`cmake --preset default`），不手拼 `-G` /
   `-DCMAKE_BUILD_TYPE`。preset 锁定了生成器（Unix Makefiles）和全部参数，
   手拼会与日常构建不一致，哈希对不上。
2. **build 目录就是 preset 的 `binaryDir`**（default→`build`，debug→`build-debug`）。
   绝不能另开 `build-warm/`——ccache 哈希含 `-I`/`-o` 的绝对路径，换目录就 miss；
   `CCACHE_BASEDIR` / `hash_dir=false` 只归一化工作目录，救不了 `-I`/`-o` 路径。
3. **Release 与 Debug 分别预热**。`-O2` 与 `-g` 是两套哈希，`build/` 暖了不代表
   `build-debug/` 暖。实测两套各约 220s。

### 实测基线

环境：5 核 / 8.2 GiB / 无 swap / Ubuntu 22.04 / GCC + Ninja。

| 场景 | 耗时 |
|---|---:|
| 真冷构建（缓存为空） | 227.8s |
| 预热后删 build 目录全量重建 | **14.8s** |
| 预热后改一个 .cpp 增量重编 | **7.2s** |
| 预热命令本身（缓存已满时） | 1~4s |

"删 build 目录全量重建"这轮打印了 **386 条 `Building CXX object`**，但 ccache
计数是 **418 hits / 0 misses**——即全部命中，没有一条真正编译。386 这个数字
反映的是 ninja 调度了多少个编译单元，不是实际编译量；判断是否真编译要看
ccache 的命中计数，不是看 make/ninja 打印了多少条。

### PCH 默认关闭

`PS_USE_PCH`（预编译头）默认 **OFF**。真实工程实测（5 核 / Ninja / 每组独立缓存）：

| 配置 | 冷构建 | 删 build 全量重建 | 增量 | 缓存体积 |
|---|---:|---:|---:|---:|
| **PCH 关 + ccache** | 194.4s | **14.8s** | **2.6s** | 16.5MB |
| PCH 开 + ccache | 246.0s | 245.6s | 5.8s | 1416MB |

PCH 在这个工程里是**纯负收益**：冷构建更慢（+27%），缓存体积涨 86 倍，
增量也慢一倍。所以默认关闭。PCH 的实验开关与实现由构建加速的另一条工作提供
（默认 `PS_USE_PCH=OFF`），仅供"没有 ccache、且反复全量重编"的场景显式开启。

用 8 文件最小工程做了机制验证（秒级，与真实工程结论一致）：

| 配置 | 删 build 重建 | 命中 | 缓存 |
|---|---:|---|---|
| PCH 关 + ccache | 0.09s | hit=8 miss=16 | 0.2MB |
| PCH 开 + ccache（无 sloppiness） | 1.55s | **hit=0, uncacheable=18** | 0MB |
| PCH 开 + ccache（有 sloppiness） | 0.21s | hit=9 | 5.6MB |

**关键机制**：PCH 不是与 ccache"硬冲突"——配上 sloppiness 后能正常命中。
真正致命的是 `CCACHE_SLOPPINESS` 缺 `pch_defines,time_macros`：此时 ccache 报
`could_not_use_precompiled_header`，那些 TU 完全绕过缓存（最小例 18/18 全废），
缓存看着在涨实际每次都在重编。环境脚本已统一写入这两个 sloppiness 项。

`-fpch-preprocess` 实测无必要（加与不加命中数相同 9/9，缓存体积相同 5.6MB），
CMake 用 `-include` 引入 PCH 已满足 ccache 要求，故不加。
同时**不要**加 `include_file_mtime`/`include_file_ctime`——那会忽略头文件时间戳，
改过头文件的 TU 命中旧缓存、编出过期代码（最小例已验证改 `pch.h` 后 9 个 TU 全部正确重编）。

### 自查

```bash
ccache --print-stats | grep -E "direct_cache_hit|preprocessed_cache_hit|cache_miss|could_not_use_precompiled_header"
```

注意 `ccache -s` 里的 Hits/Misses 是**累计值**（自上次 `ccache -z` 起），
不是单轮。要看单轮数据，先 `ccache -z` 再构建。

## 排查

| 现象 | 原因与处理 |
|---|---|
| 配置阶段卡在下载 | 走了上游 URL。确认镜像仓库已推 tag，且 `PS_DEPS_NANOARROW_URL` 首项是 Gitee 地址 |
| 解压后找不到 `CMakeLists.txt` | tag 没推上去，404 后被当成 zip 解压失败。`git ls-remote --tags <mirror>` 确认 |
| `rcc: error while loading shared libraries: libQt6Core.so.6` | 跑构建时 `LD_LIBRARY_PATH` 没包含 Qt 的 lib 目录，与依赖镜像无关 |
| `Could not find the Qt platform plugin "offscreen"` | 同上，需额外设 `QT_QPA_PLATFORM_PLUGIN_PATH` 指向 Qt 的 `plugins/platforms` |
| 哈希校验失败 | 填的哈希与实际下载源不匹配。清空 `PS_DEPS_NANOARROW_SHA256` 或填入对应源的哈希 |
