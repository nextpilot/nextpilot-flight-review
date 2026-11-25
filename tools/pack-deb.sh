#!/usr/bin/env bash
# PlotScope Debian/Ubuntu 安装包（.deb，可 apt install）。
#
# 用法：tools/pack-deb.sh [构建目录] [输出目录]
#   构建目录默认 <repo>/build，输出默认 <repo>/dist
# 依赖：dpkg-deb（Debian/Ubuntu 自带）。
#
# 复用 pack-linux.sh 的自包含 stage：二进制 RPATH=$ORIGIN/lib，
# 拷到 /usr/lib/plotscope/ 后 $ORIGIN/lib = /usr/lib/plotscope/lib，路径天然契合。
# 系统库（glibc/X11）不打包，写在 Depends 里由 apt 解析。
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${1:-$ROOT/build}"
OUT_DIR="${2:-$ROOT/dist}"
log() { printf '[pack-deb] %s\n' "$*"; }
die() { log "错误：$*" >&2; exit 1; }

command -v dpkg-deb >/dev/null || die "缺 dpkg-deb（仅 Debian/Ubuntu 可用）"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# ---- 1. 复用 pack-linux.sh 产出 stage ----
log "调用 pack-linux.sh 生成自包含 stage ..."
bash "$ROOT/tools/pack-linux.sh" "$BUILD_DIR" "$WORK" >/dev/null || die "pack-linux.sh 失败"
STAGE="$(find "$WORK" -maxdepth 1 -type d -name 'plotscope-*-linux-x86_64' | head -1)"
[ -n "$STAGE" ] || die "未找到 pack-linux.sh 的 stage 目录"
# 前置校验：stage 必须是「patchelf 已写 $ORIGIN」的形态。
# 没有 patchelf 时 pack-linux.sh 会退化成 run.sh + LD_LIBRARY_PATH 兜底，
# 而 deb 会把二进制直接装到 /usr/lib/plotscope 并裸 exec —— 装完必然找不到 .so。
# 这种包不能出，宁可当场失败。
command -v patchelf >/dev/null 2>&1 || die "缺 patchelf：stage 无相对 RPATH，deb 安装后无法加载 lib（先 apt install patchelf）"
check_rpath() {
  _rp="$(patchelf --print-rpath "$1" 2>/dev/null || true)"
  case "$_rp" in
    *'$ORIGIN/lib'*) return 0 ;;
  esac
  die "$(basename "$1") 的 RPATH 不含 \$ORIGIN/lib（实际：${_rp:-空}）"
}
check_rpath "$STAGE/plotscope-gui"
check_rpath "$STAGE/plotscope"

PKG_NAME="$(basename "$STAGE")"
VERSION="${PKG_NAME#plotscope-}"; VERSION="${VERSION%-linux-x86_64}"
# 版本单一来源（方案 D4）：versions.env 优先，保证包名不漂
[ -f "$ROOT/versions.env" ] && . "$ROOT/versions.env" && [ -n "${PS_APP_VERSION:-}" ] && VERSION="$PS_APP_VERSION"
# Debian 版本号规范化：git describe 的 -g<sha> 合法；去掉 dirty 等标记里的非法字符
DEBVER="$(echo "$VERSION" | sed -E 's/[^0-9A-Za-z.+~:-]/\./g')"
PKG="PlotScope-${VERSION}-linux-amd64"
DEBROOT="$WORK/debroot"
log "版本=$VERSION  deb 版本=$DEBVER"

# ---- 2. FHS 布局 ----
mkdir -p "$DEBROOT/DEBIAN" \
         "$DEBROOT/usr/bin" \
         "$DEBROOT/usr/lib/plotscope" \
         "$DEBROOT/usr/share/applications" \
         "$DEBROOT/usr/share/icons/hicolor/scalable/apps" \
         "$DEBROOT/usr/share/doc/plotscope"
# doc 目录不能是空的（lintian 会报 empty-package 类告警），落一份版权说明
cat > "$DEBROOT/usr/share/doc/plotscope/copyright" <<'EOF'
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: PlotScope
Source: https://gitee.com/nextpilot/nextpilot-advance-plotscope
License: BSD-3-Clause
EOF
cp "$STAGE/plotscope-gui" "$DEBROOT/usr/lib/plotscope/plotscope-gui"
cp "$STAGE/plotscope"      "$DEBROOT/usr/lib/plotscope/plotscope"
cp -a "$STAGE/lib"         "$DEBROOT/usr/lib/plotscope/lib"
cp -a "$STAGE/plugins"     "$DEBROOT/usr/lib/plotscope/plugins"
if [ -f "$STAGE/qt.conf" ]; then
  cp "$STAGE/qt.conf" "$DEBROOT/usr/lib/plotscope/qt.conf"
else
  log "警告：stage 缺 qt.conf，GUI 可能找不到 plugins/platforms"
fi

# /usr/bin 里的 wrapper（RPATH/qt.conf 在真实二进制目录已生效，这里只做转发）
cat > "$DEBROOT/usr/bin/plotscope-gui" <<'EOF'
#!/bin/sh
exec /usr/lib/plotscope/plotscope-gui "$@"
EOF
cat > "$DEBROOT/usr/bin/plotscope" <<'EOF'
#!/bin/sh
exec /usr/lib/plotscope/plotscope "$@"
EOF
chmod +x "$DEBROOT/usr/bin/plotscope-gui" "$DEBROOT/usr/bin/plotscope"

# ---- 3. 桌面项 + 内联图标（不依赖仓库图标文件）----
# 资源优先取 packaging/linux/（方案 ⑤），缺失时内联兜底
if [ -f "$ROOT/packaging/linux/plotscope.desktop" ]; then
  cp "$ROOT/packaging/linux/plotscope.desktop" "$DEBROOT/usr/share/applications/plotscope.desktop"
else
  cat > "$DEBROOT/usr/share/applications/plotscope.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=PlotScope
Comment=Advanced time-series data plotter
Exec=plotscope-gui
Icon=plotscope
Categories=Science;Utility;
Terminal=false
EOF
fi
if [ -f "$ROOT/packaging/linux/plotscope.svg" ]; then
  cp "$ROOT/packaging/linux/plotscope.svg" "$DEBROOT/usr/share/icons/hicolor/scalable/apps/plotscope.svg"
else
  cat > "$DEBROOT/usr/share/icons/hicolor/scalable/apps/plotscope.svg" <<'EOF'
<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64">
  <rect x="2" y="2" width="60" height="60" rx="10" fill="#1e2a3a"/>
  <polyline points="8,48 22,30 33,38 46,16 56,24" fill="none" stroke="#4fc3f7" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/>
  <line x1="8" y1="54" x2="56" y2="54" stroke="#607d8b" stroke-width="2"/>
</svg>
EOF
fi

# ---- 4. control（Depends 覆盖 Qt xcb 平台插件所需图形栈）----
# Installed-Size 只算 /usr（KB），不含 DEBIAN 元数据，与 dpkg 口径一致。
INST_SIZE="$(du -sk "$DEBROOT/usr" | cut -f1)"
cat > "$DEBROOT/DEBIAN/control" <<EOF
Package: plotscope
Version: ${DEBVER}
Section: science
Priority: optional
Architecture: amd64
Installed-Size: ${INST_SIZE}
Depends: libc6 (>= 2.31), libstdc++6, libgcc-s1, libxcb1, libxkbcommon-x11-0,
 libxcb-icccm4, libxcb-image0, libxcb-keysyms1, libxcb-randr0,
 libxcb-render-util0, libxcb-shape0, libxcb-xinerama0, libxcb-xkb1,
 libxcb-cursor0, libgl1
Maintainer: NextPilot <dev@nextpilot.org>
Description: Advanced time-series data plotter for robotics
 PlotScope visualizes logged time-series data (CSV / MCAP) with an
 interactive GUI and a headless CLI for batch processing.
 This package installs both the GUI (plotscope-gui) and CLI (plotscope).
EOF
cat > "$DEBROOT/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database /usr/share/applications >/dev/null 2>&1 || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor >/dev/null 2>&1 || true
EOF
chmod +x "$DEBROOT/DEBIAN/postinst"

# ---- 5. 打包 ----
mkdir -p "$OUT_DIR"
DEB_FILE="$OUT_DIR/${PKG}.deb"
dpkg-deb --build -Zxz "$DEBROOT" "$DEB_FILE" >/dev/null || die "dpkg-deb 失败"
log "打包完成：$DEB_FILE（$(du -h "$DEB_FILE" | cut -f1)）"
log "安装：sudo apt install ./$(basename "$DEB_FILE")"
