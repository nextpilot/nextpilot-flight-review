#!/usr/bin/env bash
# PlotScope Linux 发布包打包（便携 tar.gz，解压即用）。
#
#   tools/pack-linux.sh [构建目录] [输出目录]
#   默认：构建目录 = <repo>/build，输出目录 = <repo>/dist
#
# 产物：dist/plotscope-<ver>-linux-x86_64.tar.gz，解压后目录结构：
#   plotscope-gui          # strip 后的 GUI 可执行文件
#   plotscope             # strip 后的 headless CLI 可执行文件
#   lib/                   # Qt / ADS / 自定义 .so（系统库不打包）
#   plugins/platforms/     # Qt 平台插件（xcb / offscreen 等）
#   qt.conf                # 让 Qt 从包内相对路径找插件（供 GUI 用）
#   run.sh / run-cli.sh    # 兜底启动脚本（patchelf 不可用或 RPATH 失效时用）
#
# 策略：
#   - 有 patchelf → 给两个二进制和库写 $ORIGIN 相对 RPATH，不设任何环境变量即可启动；
#   - 无 patchelf → 退化到 run.sh（GUI）/ run-cli.sh（CLI）里 export LD_LIBRARY_PATH；
#   - 系统库（/lib、/usr/lib、/lib64）不打包，依赖目标机器自带 glibc/stdc++ 等基础库。
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${1:-$ROOT/build}"
OUT_DIR="${2:-$ROOT/dist}"
GUI_BIN="$BUILD_DIR/src/application/assembly/plotscope-gui"
CLI_BIN="$BUILD_DIR/src/application/app/plotscope"

log() { printf '[pack-linux] %s\n' "$*"; }
die() { log "错误：$*" >&2; exit 1; }

# GUI 与 CLI 都要发布，缺一即报错，避免产出不完整发布包。
[ -f "$GUI_BIN" ] || die "找不到 GUI ${GUI_BIN}（先构建：cmake --build <build> --target plotscope-gui plotscope）"
[ -f "$CLI_BIN" ] || die "找不到 CLI ${CLI_BIN}（构建目标需包含 plotscope）"
command -v ldd >/dev/null || die "缺 ldd"
HAS_PATCHELF=0
command -v patchelf >/dev/null 2>&1 && HAS_PATCHELF=1

# 版本：优先用二进制里的版本串，退化为 CMakeCache 的 PROJECT_VERSION，再退化为 git describe
VERSION=""
if [ -f "$ROOT/versions.env" ]; then . "$ROOT/versions.env"; VERSION="${PS_APP_VERSION:-}"; fi
[ -z "$VERSION" ] && VERSION="$(strings "$GUI_BIN" 2>/dev/null | grep -oE '^[0-9]+\.[0-9]+\.[0-9]+(-[0-9a-f]+)?$' | head -1)"
[ -z "$VERSION" ] && VERSION="$(grep -E '^CMAKE_PROJECT_VERSION:STATIC=' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | cut -d= -f2)"
[ -z "$VERSION" ] && VERSION="$(git -C "$ROOT" describe --tags --always --dirty 2>/dev/null || echo unknown)"
PKG="plotscope-${VERSION}-linux-x86_64"
STAGE="$OUT_DIR/$PKG"
ARCHIVE="$OUT_DIR/$PKG.tar.gz"

log "版本=$VERSION  构建目录=$BUILD_DIR"
if [ "$HAS_PATCHELF" = 1 ]; then
  log "patchelf: 有"
else
  log "patchelf: 无（将用 run*.sh 兜底）"
fi
rm -rf "$STAGE" && mkdir -p "$STAGE/lib" "$STAGE/plugins/platforms"

# ---- 1. strip 后的两个二进制 ----
GUI_STAGE="$STAGE/plotscope-gui"
CLI_STAGE="$STAGE/plotscope"
cp "$GUI_BIN" "$GUI_STAGE"
cp "$CLI_BIN" "$CLI_STAGE"
strip "$GUI_STAGE" 2>/dev/null || log "警告：GUI strip 失败，包体偏大"
strip "$CLI_STAGE" 2>/dev/null || log "警告：CLI strip 失败，包体偏大"
log "二进制：GUI $(du -h "$GUI_STAGE" | cut -f1)  CLI $(du -h "$CLI_STAGE" | cut -f1)"

# ---- 2. 收集动态依赖（对两个二进制取并集）----
# CLI 只依赖 Qt6::Core，是 GUI 依赖的子集；但对两个都跑 ldd 取并集更稳妥。
QT_LIB=""
for b in "$GUI_BIN" "$CLI_BIN"; do
  while IFS= read -r line; do
    case "$line" in
      *libQt6Core.so*) p="${line#*=> }"; p="${p%% (*}"; p="$(echo "$p" | xargs)";
        [ -f "$p" ] && QT_LIB="$(dirname "$p")" && break ;;
    esac
  done < <(LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}" ldd "$b" 2>/dev/null)
  [ -n "$QT_LIB" ] && break
done
[ -n "$QT_LIB" ] && log "Qt 库目录：$QT_LIB"

declare -A COPIED=()
copy_lib() {
  local src="$1" dstbase soname
  src="$(readlink -f "$src" 2>/dev/null)" || return 0
  [ -f "$src" ] || return 0
  # 关键：按 SONAME 命名（二进制按 SONAME 找库，如 libQt6Network.so.6），
  # 不能只用 realpath 的文件名（libQt6Network.so.6.4.2），否则加载器找不到。
  soname="$(patchelf --print-soname "$src" 2>/dev/null || true)"
  dstbase="${soname:-$(basename "$src")}"
  # 系统库不打包（glibc / libstdc++ / libm 等基础库由目标系统提供）
  case "$src" in
    /lib/*|/lib64/*|/usr/lib/*|/usr/lib64/*|/usr/local/lib/*) return 0 ;;
  esac
  [ -n "${COPIED[$dstbase]:-}" ] && return 0
  cp -L "$src" "$STAGE/lib/$dstbase" 2>/dev/null || return 0
  COPIED[$dstbase]=1
  # 递归收集该库自身的依赖（Qt 库之间互相依赖）
  while IFS= read -r line; do
    local p="${line#*=> }"; p="${p%% (*}"; p="$(echo "$p" | xargs)"
    case "$p" in /*) [ -f "$p" ] && copy_lib "$p" ;; esac
  done < <(LD_LIBRARY_PATH="${QT_LIB:-}" ldd "$src" 2>/dev/null | grep '=> /')
}

collect_deps_of() {
  while IFS= read -r line; do
    local_p="${line#*=> }"; local_p="${local_p%% (*}"; local_p="$(echo "$local_p" | xargs)"
    case "$local_p" in /*) [ -f "$local_p" ] && copy_lib "$local_p" ;; esac
  done < <(LD_LIBRARY_PATH="${QT_LIB:-}" ldd "$1" 2>/dev/null | grep '=> /')
}
collect_deps_of "$GUI_BIN"
collect_deps_of "$CLI_BIN"

LIB_COUNT="$(find "$STAGE/lib" -maxdepth 1 -type f | wc -l)"
log "已打包依赖库：$LIB_COUNT 个（GUI+CLI 并集）"

# ---- 3. Qt 平台插件 ----
QT_ROOT=""
[ -n "$QT_LIB" ] && QT_ROOT="$(dirname "$QT_LIB")"   # .../usr/lib/x86_64-linux-gnu -> .../usr/lib
PLUGIN_SRC=""
for d in "${QT_LIB}/qt6/plugins/platforms" "$QT_ROOT/qt6/plugins/platforms" \
         "$QT_ROOT/plugins/platforms" /opt/ng-deps/usr/lib/x86_64-linux-gnu/qt6/plugins/platforms; do
  [ -d "$d" ] && { PLUGIN_SRC="$d"; break; }
done
if [ -n "$PLUGIN_SRC" ]; then
  cp "$PLUGIN_SRC"/libqoffscreen.so "$STAGE/plugins/platforms/" 2>/dev/null || true
  cp "$PLUGIN_SRC"/libqxcb.so "$STAGE/plugins/platforms/" 2>/dev/null || true
  log "平台插件：$(ls "$STAGE/plugins/platforms" | tr '\n' ' ')"
else
  log "警告：未找到 Qt 平台插件目录，GUI 离屏启动可能失败（CLI 不受影响）"
fi

# ---- 4. RPATH（有 patchelf 时做到解压即用）----
if [ "$HAS_PATCHELF" = 1 ]; then
  # 两个二进制与库都指向包内 lib/；插件指向上一级 lib/
  patchelf --set-rpath '$ORIGIN/lib' "$GUI_STAGE" 2>/dev/null || log "警告：GUI RPATH 设置失败"
  patchelf --set-rpath '$ORIGIN/lib' "$CLI_STAGE" 2>/dev/null || log "警告：CLI RPATH 设置失败"
  for f in "$STAGE"/lib/*.so*; do
    [ -f "$f" ] && patchelf --set-rpath '$ORIGIN' "$f" 2>/dev/null || true
  done
  for f in "$STAGE"/plugins/platforms/*.so; do
    [ -f "$f" ] && patchelf --set-rpath '$ORIGIN/../../lib' "$f" 2>/dev/null || true
  done
  log "已写入相对 RPATH（\$ORIGIN）"
fi

# ---- 4b. qt.conf：让 GUI 从包内相对路径找插件（RPATH 只管 .so，管不了插件目录）----
cat > "$STAGE/qt.conf" <<'EOF'
[Paths]
Plugins = plugins
EOF
log "已写入 qt.conf（Plugins=plugins）"

# ---- 5. run.sh / run-cli.sh 兜底 ----
cat > "$STAGE/run.sh" <<'EOF'
#!/usr/bin/env bash
# GUI 兜底启动：RPATH 已生效时非必需；失效时用它补齐路径。
cd "$(dirname "$0")" || exit 1
export LD_LIBRARY_PATH="$PWD/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$PWD/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
export QT_QPA_PLATFORM_PLUGIN_PATH="$PWD/plugins/platforms${QT_QPA_PLATFORM_PLUGIN_PATH:+:$QT_QPA_PLATFORM_PLUGIN_PATH}"
exec ./plotscope-gui "$@"
EOF
cat > "$STAGE/run-cli.sh" <<'EOF'
#!/usr/bin/env bash
# headless CLI 兜底启动：RPATH 已生效时非必需；失效时用它补齐库路径。
cd "$(dirname "$0")" || exit 1
export LD_LIBRARY_PATH="$PWD/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec ./plotscope "$@"
EOF
chmod +x "$STAGE/run.sh" "$STAGE/run-cli.sh"

# ---- 6. 打 tar.gz ----
mkdir -p "$OUT_DIR"
tar -czf "$ARCHIVE" -C "$OUT_DIR" "$PKG"
log "打包完成：${ARCHIVE}（$(du -h "$ARCHIVE" | cut -f1)）"
log "解压即用：tar -xzf $(basename "$ARCHIVE") && cd $PKG && ./plotscope-gui  或  ./plotscope"
