# 发布包制作

PlotScope 每个包都含**两个可执行文件**：`plotscope-gui`（GUI 主产品）与 `plotscope`（headless CLI，批处理/服务器/CI 场景）。

## 三平台产物

| 平台 | 脚本 | 产物 | 形态 |
|---|---|---|---|
| Linux x86_64 | `tools/pack-appimage.sh` | `PlotScope-<ver>-linux-x86_64.AppImage` | 单文件，chmod +x 双击即用，Qt 闭包在包内 |
| Linux x86_64 | `tools/pack-linux.sh` | `plotscope-<ver>-linux-x86_64.tar.gz` | 便携目录，无 FUSE 环境/服务器用 |
| Windows x86_64 | `tools/pack-windows.ps1` | `PlotScope-<ver>-windows-x86_64-setup.exe` + `-portable.zip` | 安装器（开始菜单/卸载/版本信息）+ 便携 zip |
| macOS arm64 / x86_64 | `tools/pack-macos.sh` | `PlotScope-<ver>-macOS-<arch>.dmg` | `.app` 拖拽安装 |

三条命令（都在仓库根）：

```bash
tools/pack-appimage.sh build dist                      # Linux，第三参可指定 Qt 前缀
tools/pack-linux.sh    build dist                      # Linux 便携 tar.gz（兜底）
powershell -File tools\pack-windows.ps1 -BuildDir build -QtDir C:\Qt\6.2.4\msvc2019_64 -OutDir dist
tools/pack-macos.sh    build dist                      # 只在 macOS 上跑
```

前置永远相同：先构建两个目标。

```bash
cmake --build build --target plotscope-gui plotscope
```

## Linux：AppImage（首选）

用 linuxdeploy + linuxdeploy-plugin-qt + appimagetool，与 PJ4 同一条工具链。脚本首次运行会把三个工具下载到 `<repo>/.tools` 并缓存（离线环境可预先放好）。

做了什么：
1. `linuxdeploy` 收集 GUI 与 CLI 的依赖并集，Qt 库、插件、平台插件一起进 `AppDir`；
2. 覆写 `AppRun`：默认启动 GUI，`--cli` 转给 headless CLI，一个文件覆盖两种用法；
3. `appimagetool` 打成 squashfs 单文件。

用户侧：

```bash
chmod +x PlotScope-*-linux-x86_64.AppImage
./PlotScope-*-linux-x86_64.AppImage            # GUI
./PlotScope-*-linux-x86_64.AppImage --cli --help
```

无 FUSE 的容器里加 `APPIMAGE_EXTRACT_AND_RUN=1`。

`tar.gz` 仍保留，用于两类场景：目标机没有 FUSE、或只要 CLI 和 `.so` 做集成。它把系统库外置（glibc/X11 由目标机提供），因此包小但要求目标机有图形栈。

## Windows：安装器 + 便携包

`tools/pack-windows.ps1` 的流程：
1. 取 `plotscope-gui.exe` 与 `plotscope.exe`（自动兼容 MSVC 多配置的 `Release\` 与 Ninja 单配置）；
2. `windeployqt --release` 同时扫两个 exe 部署 Qt 运行库与插件；
3. 补拷 ADS 的 `ads.dll`（windeployqt 不认识）；
4. 打 `-portable.zip`；
5. 有 `makensis` 时用 `packaging/windows/installer.nsi` 出安装器，没有则只出 zip 并告警；
6. 清空 `PATH` 只留 System32 跑一次 `plotscope --help`，确保 DLL 全在 payload 里。

安装器行为：装到 `Program Files\PlotScope`、写「程序和功能」条目、建开始菜单与桌面快捷方式、提供 `PlotScope CLI`（打开命令行窗口并 cd 到 bin，不改系统 PATH）、卸载时清注册表与目录。

> Windows 包必须在 Windows 上构建（windeployqt 与 MSVC 运行时无法在 Linux 交叉得到）。本机是 Linux，这一路径由 CI 的 windows runner 验证。

## macOS：.app + dmg

`tools/pack-macos.sh` 只能在 macOS 上跑，产物架构跟随构建机（Apple Silicon 出 arm64，Intel 出 x86_64）。流程：
1. 手工组装 `PlotScope.app`（`Contents/MacOS` 放两个二进制，`Contents/Info.plist` 见 `packaging/macos/Info.plist`）；
2. 从 512 PNG 生成 `plotscope.icns`（`sips` + `iconutil`）；
3. `macdeployqt -always-overwrite` 把 Qt 框架与插件装进 `Contents/Frameworks`；
4. 签名；
5. `hdiutil` 打 UDZO dmg；
6. 可选 `notarytool` 公证 + `stapler` 装订。

签名分两档：

| 条件 | 行为 | 用户体验 |
|---|---|---|
| 设了 `MACOS_SIGNING_IDENTITY` + `APPLE_ID`/`APPLE_TEAM_ID`/`APPLE_APP_PASSWORD` | Developer ID 签名 + 公证 | 双击直接打开 |
| 只有 ad-hoc（默认） | `codesign -s -` | 首次需右键「打开」，或 `xattr -dr com.apple.quarantine PlotScope.app` |

脚本最后会跑一次 `Contents/MacOS/plotscope --help`，bundle 内依赖不全时直接失败，不会把坏包发出去。

## 图标与桌面集成

| 文件 | 用途 |
|---|---|
| `packaging/linux/plotscope.svg` | 源图（512×512，可编辑） |
| `packaging/linux/plotscope.png` / `-512.png` | AppImage 与 mac icns 的输入 |
| `packaging/linux/plotscope.desktop` | AppImage 桌面条目（`Exec=plotscope-gui`、`StartupWMClass`） |
| `packaging/windows/plotscope.ico` | 安装器与 exe 图标（16–256 六档） |

图标是几何波形 + 游标的设计稿，换品牌时只改 SVG 再重跑生成命令即可。

## CI

`.github/workflows/release.yml` 三 job（ubuntu-22.04 / windows-2022 / macos-14）+ 一个汇总 job：

- 推 `v*` tag 或手动触发；
- 每个 job 各自 configure + build 两个目标，再调对应打包脚本；
- Linux job 出包后 `env -i` 清空环境跑 `--cli --help` 冒烟；
- tag 构建把 `*.AppImage` / `*.dmg` / `*.exe` / `*.zip` 一起挂到 Release。

**注意**：这是 GitHub Actions 配置，Gitee 主仓不会执行。要用 CI 出包，需要仓库镜像到 GitHub；否则在本机/自建机直接跑上表脚本。macOS 与 Windows 两个 job 无法用 Linux 交叉替代。

## 体积与瘦身

| 口径 | 大小 | 说明 |
|---|---|---|
| `plotscope-gui`（strip 后） | 约 12.8 MB | 未 strip 25.3 MB |
| `plotscope` CLI（strip 后） | 约 7.7 MB | 只链 Core |
| tar.gz（系统库外置） | 约 17 MB / 解压 50 MB | 要求目标机有图形栈 |
| 全自包含 | 约 97 MB | Qt + 图形栈全打进去 |

AppImage 走的是"全自包含 + squashfs 压缩"，分发体比 tar.gz 大，但**用户侧不需要装任何东西**，跨发行版不会因缺 xcb 库启动失败。这是与 PJ4 一致的选择。

进一步瘦身的手段（按性价比）：
1. 只带实际用到的 Qt 插件（linuxdeploy-plugin-qt 已按依赖裁剪）；
2. 默认 strip（脚本已做）；
3. 裁 ICU（Qt 6.2 的 `libicudata` 约 29 MB，需确认无中文/正则 Unicode 依赖，属产品决策）；
4. AppImage 换 zstd 压缩（`appimagetool --compression zstd`，解压更快、体积略小）。

## 已知局限

1. macOS 未签名时 Gatekeeper 拦截，用户需手动放行；要根治需 Apple Developer 账号。
2. Windows 安装器未代码签名，SmartScreen 会提示"未知发布者"。
3. macOS 只出当前构建机架构；要 Universal 二进制需 `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` 重编 Qt 依赖闭包。
4. Linux AppImage 未做 glibc 版本审计；若在更新的发行版上构建，老系统会因 `GLIBC_2.3x` 缺失跑不起来（PJ4 用固定 22.04 容器解决，我们 CI 也钉 `ubuntu-22.04`）。
5. `setup 脚本发现系统 Qt` 的既有问题仍在（`find_qt()` 只认固定路径），不阻塞打包。
