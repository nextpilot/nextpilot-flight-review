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

ccache 本身由 `tools/setup-build-env.sh` 安装（自包含目录落 `~/.ps-deps/ccache/`，
并向 `~/.bashrc`、`~/.profile` 幂等写入 `PATH` / `CCACHE_DIR` / `CCACHE_MAXSIZE` / `CCACHE_BASEDIR`），
支持 `--check` / `--install` / `--uninstall`。因为 `CMakeLists.txt` 里的探测靠 shell 的 `PATH`，
换机器后重跑一次脚本即可，不需要改仓库配置。

`tools/setup-dev-env.sh` 是更完整的一键环境体检（工具链 + Qt + shell 变量）。

## 排查

| 现象 | 原因与处理 |
|---|---|
| 配置阶段卡在下载 | 走了上游 URL。确认镜像仓库已推 tag，且 `PS_DEPS_NANOARROW_URL` 首项是 Gitee 地址 |
| 解压后找不到 `CMakeLists.txt` | tag 没推上去，404 后被当成 zip 解压失败。`git ls-remote --tags <mirror>` 确认 |
| `rcc: error while loading shared libraries: libQt6Core.so.6` | 跑构建时 `LD_LIBRARY_PATH` 没包含 Qt 的 lib 目录，与依赖镜像无关 |
| `Could not find the Qt platform plugin "offscreen"` | 同上，需额外设 `QT_QPA_PLATFORM_PLUGIN_PATH` 指向 Qt 的 `plugins/platforms` |
| 哈希校验失败 | 填的哈希与实际下载源不匹配。清空 `PS_DEPS_NANOARROW_SHA256` 或填入对应源的哈希 |
