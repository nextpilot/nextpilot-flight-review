#!/usr/bin/env bash
# 安装并配置本仓库的编译环境：工具链检查 → 缺什么补什么 → Qt 6.2.4 → ccache 与 shell 变量。
# 环境差异全部留在用户目录与 shell 启动文件里，仓库只认约定路径，CMakeLists 不写死任何绝对路径。
#
#   tools/setup-dev-env.sh --check          # 只体检，不改任何东西（默认）
#   tools/setup-dev-env.sh --install        # 补齐缺失项并写入 ~/.bashrc、~/.profile
#   tools/setup-dev-env.sh --install --qt   # 只处理 Qt
#   tools/setup-dev-env.sh --install --toolchain   # 只处理工具链与 ccache
#
# Qt 装到哪：优先沿用已有的（仓库内 deps/ 或 $PS_DEPS_DIR），都没有才新装到 $PS_DEPS_DIR。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="${PS_DEPS_DIR:-$HOME/.ps-deps}"
QT_VERSION="${QT_VERSION:-6.2.4}"
MIN_GCC=9
MIN_CMAKE=3.22

MARK_BEGIN="# >>> plotscope qt env >>>"
MARK_END="# <<< plotscope qt env <<<"
STARTUP_FILES=("${BASHRC:-$HOME/.bashrc}" "${PROFILE:-$HOME/.profile}")

MODE="--check"
DO_QT=1
DO_TOOLCHAIN=1
for arg in "$@"; do
  case "$arg" in
    --check | --install) MODE="$arg" ;;
    --qt | --toolchain)
      if [ "$arg" = "--qt" ]; then DO_TOOLCHAIN=0; else DO_QT=0; fi
      ;;
    -h | --help)
      sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      echo "未知参数：$arg" >&2
      exit 2
      ;;
  esac
done

log() { printf '[dev-env] %s\n' "$*"; }
ok() { printf '  ✓ %s\n' "$*"; }
bad() { printf '  ✗ %s\n' "$*"; }

ver_ge() { # ver_ge <实际> <要求>
  [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -1)" = "$2" ]
}

# ---- 探测：Qt 装在哪 ----
find_qt() {
  local c
  for c in "$DEPS/Qt/$QT_VERSION/gcc_64" "$ROOT/deps/Qt/$QT_VERSION/gcc_64" "$DEPS/Qt/$QT_VERSION/gcc_64"; do
    [ -f "$c/lib/cmake/Qt6/Qt6Config.cmake" ] && {
      echo "$c"
      return 0
    }
  done
  return 1
}

# ---- 体检 ----
MISSING_PKGS=()
check_toolchain() {
  log "工具链"
  local gcc_v cmake_v
  if command -v gcc >/dev/null 2>&1; then
    gcc_v="$(gcc -dumpversion)"
    ver_ge "$gcc_v" "$MIN_GCC" && ok "gcc $gcc_v（需 ≥ $MIN_GCC）" || bad "gcc $gcc_v 低于 $MIN_GCC"
  else
    bad "gcc 未安装"
    MISSING_PKGS+=(build-essential)
  fi
  command -v g++ >/dev/null 2>&1 && ok "g++ 就绪" || bad "g++ 未安装"

  if command -v cmake >/dev/null 2>&1; then
    cmake_v="$(cmake --version | head -1 | sed 's/cmake version //')"
    ver_ge "${cmake_v%%-*}" "$MIN_CMAKE" && ok "cmake $cmake_v（需 ≥ $MIN_CMAKE）" || bad "cmake $cmake_v 低于 $MIN_CMAKE"
  else
    bad "cmake 未安装"
    MISSING_PKGS+=(cmake)
  fi

  if command -v ninja >/dev/null 2>&1; then
    ok "ninja $(ninja --version)"
  else
    bad "ninja 未安装（可选，先装后可用 -G Ninja 提速）"
    MISSING_PKGS+=(ninja-build)
  fi

  command -v git >/dev/null 2>&1 && ok "git $(git --version | awk '{print $3}')" || {
    bad "git 未安装"
    MISSING_PKGS+=(git)
  }
  command -v python3 >/dev/null 2>&1 && ok "python3 $(python3 --version 2>&1 | awk '{print $2}')" || {
    bad "python3 未安装"
    MISSING_PKGS+=(python3)
  }

  if "$DEPS/ccache/bin/ccache" --version >/dev/null 2>&1; then
    ok "ccache $("$DEPS/ccache/bin/ccache" --version | head -1 | awk '{print $3}')（$DEPS/ccache）"
  elif command -v ccache >/dev/null 2>&1; then
    bad "ccache 在 PATH 中但没落持久目录，跑 tools/setup-build-env.sh 固化"
  else
    bad "ccache 未安装（可选，但强烈建议）"
    MISSING_PKGS+=(ccache)
  fi
}

check_qt() {
  log "Qt $QT_VERSION"
  local qt
  if qt="$(find_qt)"; then
    ok "已装：$qt"
    QT_DIR="$qt"
  else
    bad "未找到（会装到 $DEPS/Qt/$QT_VERSION/gcc_64）"
    MISSING_PKGS+=(3d 系统依赖)
  fi
}

# Qt 6.2.4 官方 xcb 平台插件所需系统库，headless 测试另需 mesa 的 libEGL
QT_SYS_PKGS=(
  libgl1-mesa-dev libglu1-mesa libegl1 libxkbcommon0 libxkbcommon-x11-0
  libdbus-1-3 libfontconfig1 libglib2.0-0 libfreetype6
  libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1
  libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libx11-xcb1
)

# ---- 安装 ----
apt_install() {
  local pkgs=("$@")
  [ ${#pkgs[@]} -eq 0 ] && return 0
  command -v apt-get >/dev/null 2>&1 || {
    log "非 Debian 系系统，请手动安装：${pkgs[*]}"
    return 1
  }
  log "apt 安装：${pkgs[*]}"
  sudo apt-get update -qq
  sudo DEBIAN_FRONTEND=noninteractive apt-get install -y "${pkgs[@]}"
}

install_toolchain() {
  local uniq=()
  local p
  for p in "${MISSING_PKGS[@]}"; do
    [ "$p" = "3d 系统依赖" ] && continue
    [[ " ${uniq[*]} " == *" $p "* ]] || uniq+=("$p")
  done
  apt_install "${uniq[@]}" || true
  MISSING_PKGS=()
  check_toolchain || true
}

install_qt() {
  if find_qt >/dev/null; then
    log "Qt 已存在，跳过下载"
    return 0
  fi
  command -v python3 >/dev/null 2>&1 && apt_install python3-pip || true
  python3 -m pip install --user --quiet aqtinstall
  log "下载 Qt $QT_VERSION（qtbase/qtsvg/qttools/icu，约 200MB）到 $DEPS/Qt"
  python3 -m aqt install-qt linux desktop "$QT_VERSION" gcc_64 \
    -O "$DEPS/Qt" --archives qtbase qtsvg qttools icu
  find_qt >/dev/null && log "Qt 就绪：$(find_qt)" || {
    log "Qt 安装失败，检查网络或改用 --archives qtbase"
    return 1
  }
}

strip_block() {
  local f="$1"
  [ -f "$f" ] || return 0
  awk -v b="$MARK_BEGIN" -v e="$MARK_END" '
    $0 == b { skip = 1; next }
    $0 == e { skip = 0; next }
    !skip { print }
  ' "$f" >"$f.$$.tmp" && mv "$f.$$.tmp" "$f"
}

write_qt_block() {
  local qt="$1" f
  for f in "${STARTUP_FILES[@]}"; do
    touch "$f"
    strip_block "$f"
    cat >>"$f" <<EOF
$MARK_BEGIN
export PS_QT_DIR="$qt"
export CMAKE_PREFIX_PATH="\$PS_QT_DIR\${CMAKE_PREFIX_PATH:+:\$CMAKE_PREFIX_PATH}"
export PATH="\$PS_QT_DIR/bin:\$PATH"
export LD_LIBRARY_PATH="\$PS_QT_DIR/lib\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
$MARK_END
EOF
    log "已写入 $f"
  done
}

report_env() {
  local qt
  qt="$(find_qt || true)"
  echo
  log "环境摘要"
  [ -n "$qt" ] && echo "  Qt：$qt"
  echo "  DEPS：$DEPS"
  echo "  建议命令：cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPS_DEPS_FETCH=ON && cmake --build build -j2"
  echo "  提示：新开终端生效；当前终端可 source ~/.bashrc"
}

case "$MODE" in
  --check)
    [ "$DO_TOOLCHAIN" = 1 ] && check_toolchain
    [ "$DO_QT" = 1 ] && check_qt
    report_env
    ;;
  --install)
    if [ "$DO_TOOLCHAIN" = 1 ]; then
      check_toolchain
      install_toolchain
      apt_install "${QT_SYS_PKGS[@]}" || true
      [ -x "$ROOT/tools/setup-build-env.sh" ] && bash "$ROOT/tools/setup-build-env.sh" --install || true
    fi
    if [ "$DO_QT" = 1 ]; then
      install_qt
      qt="$(find_qt || true)"
      [ -n "$qt" ] && write_qt_block "$qt"
    fi
    report_env
    ;;
esac
