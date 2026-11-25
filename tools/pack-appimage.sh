#!/usr/bin/env bash
# PlotScope Linux AppImage 打包（单文件、双击即用、自带 Qt 闭包）。
#
#   tools/pack-appimage.sh [构建目录] [输出目录] [Qt目录]
#   默认：构建目录 = <repo>/build，输出目录 = <repo>/dist
#
# 产物：dist/PlotScope-<ver>-linux-x86_64.AppImage
#
# 与 pack-linux.sh（便携 tar.gz）的分工：
#   - AppImage 面向终端用户：一个文件，chmod +x 后双击/直接运行，Qt 依赖全在
#     只读 squashfs 里，不要求目标机装任何图形库；启动时可选自解压到 /tmp。
#   - tar.gz 面向无 FUSE 环境（容器/服务器）与只想拿 CLI 的场景。
#
# 实现：linuxdeploy + linuxdeploy-plugin-qt + appimagetool（与 PJ4 同一条工具链）。
# 工具首次运行时下载到 <repo>/.tools 并缓存；离线环境可预先放好再跑。
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${1:-$ROOT/build}"
OUT_DIR="${2:-$ROOT/dist}"
QT_DIR="${3:-}"

GUI_BIN="$BUILD_DIR/src/application/assembly/plotscope-gui"
CLI_BIN="$BUILD_DIR/src/application/app/plotscope"
TOOLS_DIR="$ROOT/.tools"
ARCH="${ARCH:-x86_64}"

log() { printf '[appimage] %s\n' "$*"; }
die() { log "错误：$*" >&2; exit 1; }

[ -f "$GUI_BIN" ] || die "找不到 GUI ${GUI_BIN}（先构建：cmake --build <build> --target plotscope-gui plotscope）"
[ -f "$CLI_BIN" ] || die "找不到 CLI $CLI_BIN"

# Qt 目录：参数优先，其次从 CMakeCache 的 Qt6_DIR 反推，最后用仓库内 deps/Qt。
if [ -z "$QT_DIR" ]; then
  _qt6_dir="$(grep -E '^Qt6_DIR:PATH=' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | cut -d= -f2-)"
  if [ -n "$_qt6_dir" ] && [ -d "$_qt6_dir" ]; then
    QT_DIR="$(cd "$(dirname "$_qt6_dir")/../.." && pwd)"
  fi
fi
if [ -z "$QT_DIR" ] || [ ! -x "$QT_DIR/bin/qmake" ]; then
  for c in "$ROOT/deps/Qt/6.2.4/gcc_64" "${PS_DEPS_PREFIX:-}/usr"; do
    [ -x "$c/bin/qmake" ] && QT_DIR="$c" && break
  done
fi
[ -n "$QT_DIR" ] && [ -x "$QT_DIR/bin/qmake" ] || die "找不到 Qt（qmake）。用第 3 个参数指定 Qt 前缀"
log "Qt=$QT_DIR"

VERSION=""
if [ -f "$ROOT/versions.env" ]; then . "$ROOT/versions.env"; VERSION="${PS_APP_VERSION:-}"; fi
[ -z "$VERSION" ] && VERSION="$(strings "$GUI_BIN" 2>/dev/null | grep -oE '^[0-9]+\.[0-9]+\.[0-9]+(-[0-9a-f]+)?$' | head -1)"
[ -z "$VERSION" ] && VERSION="$(grep -E '^CMAKE_PROJECT_VERSION:STATIC=' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | cut -d= -f2)"
[ -z "$VERSION" ] && VERSION="$(git -C "$ROOT" describe --tags --always 2>/dev/null || echo 0.0.0)"
log "版本=$VERSION  构建目录=$BUILD_DIR"

mkdir -p "$OUT_DIR" "$TOOLS_DIR"

fetch() {
  local name="$1" url="$2" out="$TOOLS_DIR/$1"
  if [ -x "$out" ]; then return 0; fi
  log "下载 $name"
  curl -fL --retry 3 --connect-timeout 15 -o "$out.tmp" "$url" || die "下载失败：$url"
  chmod +x "$out.tmp" && mv "$out.tmp" "$out"
}

LD="$TOOLS_DIR/linuxdeploy-$ARCH.AppImage"
LDQT="$TOOLS_DIR/linuxdeploy-plugin-qt-$ARCH.AppImage"
AIT="$TOOLS_DIR/appimagetool-$ARCH.AppImage"
fetch "linuxdeploy-$ARCH.AppImage" "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$ARCH.AppImage" "$LD"
fetch "linuxdeploy-plugin-qt-$ARCH.AppImage" "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-$ARCH.AppImage" "$LDQT"
fetch "appimagetool-$ARCH.AppImage" "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-$ARCH.AppImage" "$AIT"

# linuxdeploy 按不带架构后缀的名字去找插件与 appimagetool，这里建软链。
ln -sfn "$LD" "$TOOLS_DIR/linuxdeploy"
ln -sfn "$LDQT" "$TOOLS_DIR/linuxdeploy-plugin-qt"
ln -sfn "$AIT" "$TOOLS_DIR/appimagetool"

# 容器/无 FUSE 环境：让 AppImage 自解压运行。
export APPIMAGE_EXTRACT_AND_RUN=1
export ARCH
export QMAKE="$QT_DIR/bin/qmake"
export LD_LIBRARY_PATH="$QT_DIR/lib:${LD_LIBRARY_PATH:-}"
# linuxdeploy-plugin-qt 通过 PATH 找同目录下的 linuxdeploy 与插件。
export PATH="$TOOLS_DIR:$PATH"

APPDIR="$OUT_DIR/AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR"

# 图标：desktop 里 Icon=plotscope，AppDir 根需存在 plotscope.png（.DirIcon 由工具生成）。
cp "$ROOT/packaging/linux/plotscope.png" "$APPDIR/plotscope.png"
cp "$ROOT/packaging/linux/plotscope-512.png" "$APPDIR/plotscope-512.png" 2>/dev/null || true

log "linuxdeploy 收集依赖（Qt 插件 + GUI/CLI 两个可执行文件）"
"$LD" --appdir "$APPDIR" \
  --executable "$GUI_BIN" \
  --executable "$CLI_BIN" \
  --desktop-file "$ROOT/packaging/linux/plotscope.desktop" \
  --icon-file "$ROOT/packaging/linux/plotscope.png" \
  --plugin qt 2>&1 | sed 's/^/[linuxdeploy] /' || die "linuxdeploy 失败"

# 这里不能用 `--output appimage`：linuxdeploy 会在补 offscreen 插件、覆写 AppRun
# 之前就把 AppImage 打出来，产物只带 xcb 且 AppRun 还是它自己的软链版本，
# 无头冒烟必挂。先只做 AppDir，最后一步用 appimagetool 统一打包。

# 无头环境（CI/服务器）用 offscreen 插件跑冒烟；linuxdeploy 只按依赖挑到了 xcb。
if [ -f "$QT_DIR/plugins/platforms/libqoffscreen.so" ]; then
  cp -f "$QT_DIR/plugins/platforms/libqoffscreen.so" "$APPDIR/usr/plugins/platforms/"
fi

# 覆写 AppRun：默认入口是 GUI；`--cli` 转给 headless CLI，方便服务器/CI 用同一个文件。
# 注意 linuxdeploy 建的 AppRun 是软链（指向 usr/bin/plotscope-gui），必须先删，
# 否则 cat 会穿透软链覆盖真正的二进制。
rm -f "$APPDIR/AppRun"
cat > "$APPDIR/AppRun" <<'APPRUN'
#!/bin/sh
set -e
HERE="$(dirname "$(readlink -f "$0")")"
export QT_QPA_PLATFORM_PLUGIN_PATH="$HERE/usr/plugins/platforms"
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
if [ "${1:-}" = "--cli" ]; then
  shift
  exec "$HERE/usr/bin/plotscope" "$@"
fi
exec "$HERE/usr/bin/plotscope-gui" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"

# 打包必须发生在「补 offscreen 插件 + 覆写 AppRun」之后，顺序错了就是坏包。
DEST="$OUT_DIR/PlotScope-${VERSION}-linux-${ARCH}.AppImage"
log "appimagetool 打包：$DEST"
"$AIT" --no-appstream "$APPDIR" "$DEST" 2>&1 | sed 's/^/[appimagetool] /' || die "appimagetool 失败"
[ -f "$DEST" ] || die "未生成 $DEST"
chmod +x "$DEST"
log "完成：${DEST}（$(du -h "$DEST" | cut -f1)）"
