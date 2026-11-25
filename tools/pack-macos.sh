#!/usr/bin/env bash
# PlotScope macOS 发布包：PlotScope.app + PlotScope-<ver>-macOS-<arch>.dmg
#
#   tools/pack-macos.sh [构建目录] [输出目录] [Qt目录]
#
# 必须在 macOS 上运行（macdeployqt / sips / iconutil / codesign / hdiutil 都是系统工具）。
# 产出的架构与当前机器一致：Apple Silicon 出 arm64，Intel 出 x86_64。
#
# 签名：
#   - 默认 ad-hoc（codesign -s -），dmg 能开机即用，但下载来的包会被 Gatekeeper 拦一次，
#     用户需右键「打开」或在终端 xattr -dr com.apple.quarantine 放行（文档里写明）。
#   - 有 Developer ID 时设 MACOS_SIGNING_IDENTITY，再加 APPLE_ID / APPLE_TEAM_ID /
#     APPLE_APP_PASSWORD 可走 notarytool 公证，用户双击无警告。
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${1:-$ROOT/build}"
OUT_DIR="${2:-$ROOT/dist}"
QT_DIR="${3:-}"

log() { printf '[pack-macos] %s\n' "$*"; }
die() { log "错误：$*" >&2; exit 1; }

[ "$(uname -s)" = "Darwin" ] || die "本脚本只能在 macOS 上运行（当前 $(uname -s)）"

GUI_BIN="$BUILD_DIR/src/application/assembly/plotscope-gui"
CLI_BIN="$BUILD_DIR/src/application/app/plotscope"
[ -f "$GUI_BIN" ] || die "找不到 GUI $GUI_BIN"
[ -f "$CLI_BIN" ] || die "找不到 CLI $CLI_BIN"

if [ -z "$QT_DIR" ]; then
  _qt6_dir="$(grep -E '^Qt6_DIR:PATH=' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | cut -d= -f2-)"
  [ -n "$_qt6_dir" ] && QT_DIR="$(cd "$(dirname "$_qt6_dir")/../.." && pwd)"
fi
[ -n "$QT_DIR" ] && [ -x "$QT_DIR/bin/macdeployqt" ] || die "找不到 Qt（macdeployqt），用第 3 个参数指定"

case "$(uname -m)" in
  arm64) ARCH=arm64 ;;
  x86_64) ARCH=x86_64 ;;
  *) ARCH="$(uname -m)" ;;
esac

VERSION=""
if [ -f "$ROOT/versions.env" ]; then . "$ROOT/versions.env"; VERSION="${PS_APP_VERSION:-}"; fi
[ -z "$VERSION" ] && VERSION="$(strings "$GUI_BIN" 2>/dev/null | grep -oE '^[0-9]+\.[0-9]+\.[0-9]+(-[0-9a-f]+)?$' | head -1)"
[ -z "$VERSION" ] && VERSION="$(grep -E '^CMAKE_PROJECT_VERSION:STATIC=' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | cut -d= -f2)"
[ -z "$VERSION" ] && VERSION="$(git -C "$ROOT" describe --tags --always 2>/dev/null || echo 0.0.0)"
log "版本=$VERSION  架构=$ARCH  Qt=$QT_DIR"

APP="$OUT_DIR/PlotScope.app"
DMG="$OUT_DIR/PlotScope-${VERSION}-macOS-${ARCH}.dmg"
rm -rf "$APP" "$DMG"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

cp "$GUI_BIN" "$APP/Contents/MacOS/plotscope-gui"
cp "$CLI_BIN" "$APP/Contents/MacOS/plotscope"
chmod +x "$APP/Contents/MacOS/plotscope-gui" "$APP/Contents/MacOS/plotscope"

# Info.plist（版本占位替换）
sed "s/@VERSION@/${VERSION}/g" "$ROOT/packaging/macos/Info.plist" > "$APP/Contents/Info.plist"

# 图标：从 512 PNG 生成多尺寸 iconset 再转 icns。
ICONSET="$OUT_DIR/plotscope.iconset"
rm -rf "$ICONSET" && mkdir -p "$ICONSET"
for s in 16 32 64 128 256 512; do
  sips -z "$s" "$s" "$ROOT/packaging/linux/plotscope-512.png" --out "$ICONSET/icon_${s}x${s}.png" >/dev/null 2>&1
  d=$((s * 2))
  sips -z "$d" "$d" "$ROOT/packaging/linux/plotscope-512.png" --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null 2>&1
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/plotscope.icns" || log "警告：icns 生成失败，用默认图标"

log "macdeployqt 部署 Qt 框架与插件"
# 不要传 -no-codesign：Qt 6.2.4 的 macdeployqt 不认这个参数，会直接报
# 'ERROR: Unknown argument "-no-codesign"' 并返回非 0（build-packages run 45676009156）。
# 签名统一由下面的 codesign 处理（--force 会覆盖 macdeployqt 的默认签名）。
"$QT_DIR/bin/macdeployqt" "$APP" -always-overwrite 2>&1 | sed 's/^/[macdeployqt] /' || die "macdeployqt 失败"

# CLI 也要能独立跑：macdeployqt 只处理主程序，CLI 依赖 Core，把 Core 框架已存在的即可。
# 若 CLI 需要额外框架，macdeployqt 已随 GUI 一起拷入 Frameworks。

# 签名
if [ -n "${MACOS_SIGNING_IDENTITY:-}" ]; then
  log "Developer ID 签名：$MACOS_SIGNING_IDENTITY"
  codesign --force --deep --options runtime --timestamp \
    --sign "$MACOS_SIGNING_IDENTITY" "$APP" || die "codesign 失败"
else
  log "ad-hoc 签名（无 Developer ID，用户首次运行需手动放行）"
  codesign --force --deep --sign - "$APP" || die "ad-hoc codesign 失败"
fi
codesign --verify --deep --strict "$APP" && log "签名校验通过"

# 冒烟：CLI 必须能从 bundle 内跑起来。
"$APP/Contents/MacOS/plotscope" --help >/dev/null 2>&1 || die "CLI 冒烟失败（bundle 内依赖不全）"
log "CLI 冒烟通过"

log "打 dmg"
hdiutil create -volname "PlotScope $VERSION" -srcfolder "$APP" -ov -format UDZO "$DMG" >/dev/null || die "hdiutil 失败"

# 公证（需要 Apple 账号三件套）
if [ -n "${MACOS_SIGNING_IDENTITY:-}" ] && [ -n "${APPLE_ID:-}" ] && [ -n "${APPLE_TEAM_ID:-}" ] && [ -n "${APPLE_APP_PASSWORD:-}" ]; then
  log "提交公证（notarytool）"
  xcrun notarytool submit "$DMG" --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" \
    --password "$APPLE_APP_PASSWORD" --wait || die "公证失败"
  xcrun stapler staple "$DMG" || log "警告：staple 失败，用户需联网校验"
else
  log "跳过公证（未提供 APPLE_ID/APPLE_TEAM_ID/APPLE_APP_PASSWORD）"
fi

log "完成：$DMG（$(du -h "$DMG" | cut -f1)）"
