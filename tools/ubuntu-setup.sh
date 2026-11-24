#!/usr/bin/env bash
# PlotScope 编译环境统一入口（Ubuntu/Debian）。
#
#   tools/ubuntu-setup.sh --check                 # 只体检，不改任何文件（默认）
#   tools/ubuntu-setup.sh --install               # 全量：工具链 + Qt + Rust + Python venv + shell 块
#   tools/ubuntu-setup.sh --install --toolchain   # 只处理 cmake / ninja / ccache（gcc 只体检，不自动装）
#   tools/ubuntu-setup.sh --install --qt          # 只处理 Qt
#   tools/ubuntu-setup.sh --install --python      # 只处理 venv 与 pip 依赖
#   tools/ubuntu-setup.sh --install --rust        # 只处理 rustup 工具链
#   tools/ubuntu-setup.sh --print-env             # 输出可 eval 的环境（CI 用）
#   tools/ubuntu-setup.sh --uninstall             # 移除写进 shell 的块（保留已装目录）
#
# 原则：系统自带且版本达标就用系统，不达标统一装进 $PS_DEPS_ROOT；版本只从
# 仓库根 versions.json 读，本脚本不写死任何版本号。
set -uo pipefail

if [ ! -f /etc/debian_version ] || [ "$(uname -s)" != "Linux" ]; then
  echo "[ubuntu-setup] 本脚本只支持 Ubuntu/Debian；Windows 见 docs/develop/building.md" >&2
  exit 2
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export PS_DEPS_ROOT="${PS_DEPS_ROOT:-$HOME/.ps-deps}"
MANIFEST="$ROOT/versions.json"
STATE_DIR="$PS_DEPS_ROOT/state"
STATE_FILE="$STATE_DIR/toolchain.json"
MARK_BEGIN="# >>> plotscope build env >>>"
MARK_END="# <<< plotscope build env <<<"
STARTUP_FILES=("${BASHRC:-$HOME/.bashrc}" "${PROFILE:-$HOME/.profile}")

MODE="--check"
DO_TOOLCHAIN=1
DO_QT=1
DO_PYTHON=1
DO_RUST=1
for arg in "$@"; do
  case "$arg" in
    --check | --install | --print-env | --uninstall) MODE="$arg" ;;
    --toolchain) DO_QT=0; DO_PYTHON=0; DO_RUST=0 ;;
    --qt) DO_TOOLCHAIN=0; DO_PYTHON=0; DO_RUST=0 ;;
    --python) DO_TOOLCHAIN=0; DO_QT=0; DO_RUST=0 ;;
    --rust) DO_TOOLCHAIN=0; DO_QT=0; DO_PYTHON=0 ;;
    -h | --help) sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "未知参数：$arg" >&2; exit 2 ;;
  esac
done

log() { printf '[ubuntu-setup] %s\n' "$*"; }
row() { printf '  %-8s %-10s %-12s %-10s %s\n' "$1" "$2" "$3" "$4" "$5"; }

# ---------- versions.json ----------
if [ ! -f "$MANIFEST" ]; then
  log "缺少 $MANIFEST"
  exit 1
fi
if ! python3 -m json.tool "$MANIFEST" >/dev/null 2>&1; then
  log "versions.json 语法错误：$MANIFEST"
  exit 1
fi

json_get() { # json_get qt version / json_get toolchain gcc min —— 逐层取键
  python3 -c 'import json,sys
d=json.load(open(sys.argv[1]))
for k in sys.argv[2:]:
    d=d[k]
print(d)' "$MANIFEST" "$@" 2>/dev/null
}

V_QT="$(json_get toolchains qt pin)"
QT_ABI="$(json_get toolchains qt abi)"
V_GCC_MIN="$(json_get toolchains gcc min)"
V_CMAKE_MIN="$(json_get toolchains cmake min)"
V_NINJA_MIN="$(json_get toolchains ninja min)"
V_CCACHE_MIN="$(json_get toolchains ccache min)"
V_PY_MIN="$(json_get toolchains python min)"
V_PY_REQ="$(json_get toolchains python configuration)"

# Rust 的实际版本写在 rust-toolchain.toml（cargo 直读），versions.json 只给门槛 min 与文件路径
V_RUST_MIN="$(json_get toolchains rust min)"
V_RUST_CFG="$(json_get toolchains rust configuration)"
RUST_TOML="$ROOT/${V_RUST_CFG:-src/kernel/rust-toolchain.toml}"
V_RUST_CHANNEL="$([ -f "$RUST_TOML" ] && sed -n 's/^channel *= *"\(.*\)"/\1/p' "$RUST_TOML" | head -1)"
V_RUST_REQ="${V_RUST_CHANNEL:-$V_RUST_MIN}"

ver_ge() { [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -1)" = "$2" ]; }

# 来源判定看实际解析到的可执行文件：落在统一根下记为 managed（如软链的 cmake）
src_of() {
  local p; p="$(command -v "$1" 2>/dev/null)" || { echo none; return; }
  case "$p" in "$PS_DEPS_ROOT"*) echo managed ;; *) echo system ;; esac
}

# ---------- 探测 ----------
probe_cmake() { command -v cmake >/dev/null 2>&1 && cmake --version | head -1 | sed 's/cmake version //'; }
probe_ninja() { command -v ninja >/dev/null 2>&1 && ninja --version; }
probe_gcc() { command -v gcc >/dev/null 2>&1 && gcc -dumpversion; }
probe_python() { command -v python3 >/dev/null 2>&1 && python3 -c 'import sys;print("%d.%d.%d"%sys.version_info[:3])'; }
probe_ccache() {
  if [ -x "$PS_DEPS_ROOT/toolchain/ccache/bin/ccache" ]; then
    "$PS_DEPS_ROOT/toolchain/ccache/bin/ccache" --version | head -1 | awk '{print $3}'
  elif command -v ccache >/dev/null 2>&1; then
    ccache --version | head -1 | awk '{print $3}'
  fi
}
probe_rust() {
  local c=""
  if [ -x "$PS_DEPS_ROOT/toolchain/rust/cargo/bin/cargo" ]; then c="$PS_DEPS_ROOT/toolchain/rust/cargo/bin/cargo"
  elif [ -x "$HOME/.cargo/bin/cargo" ]; then c="$HOME/.cargo/bin/cargo"
  elif command -v cargo >/dev/null 2>&1; then c="cargo"
  fi
  [ -n "$c" ] && "$c" --version | awk '{print $2}'
}
find_qt() {
  local c
  for c in "$PS_DEPS_ROOT/toolchain/qt/$V_QT/$QT_ABI" "$ROOT/deps/Qt/$V_QT/$QT_ABI"; do
    [ -f "$c/lib/cmake/Qt6/Qt6Config.cmake" ] && { echo "$c"; return 0; }
  done
  return 1
}
qt_source() {
  local q; q="$(find_qt)" || return 1
  case "$q" in "$PS_DEPS_ROOT"*) echo managed ;; *) echo repo-deps ;; esac
}

# rust 变量只指向真正装在那里的位置：统一目录有 cargo 才覆盖 RUSTUP_HOME/CARGO_HOME，
# 否则用系统 ~/.cargo（覆盖成空目录会让 rustup shim 找不到 toolchain 与 rustc）。
rust_env() {
  if [ -x "$PS_DEPS_ROOT/toolchain/rust/cargo/bin/cargo" ]; then
    echo "export RUSTUP_HOME=\"$PS_DEPS_ROOT/toolchain/rust/rustup\""
    echo "export CARGO_HOME=\"$PS_DEPS_ROOT/toolchain/rust/cargo\""
    echo "export PATH=\"$PS_DEPS_ROOT/toolchain/rust/cargo/bin:\$PATH\""
  elif [ -x "$HOME/.cargo/bin/cargo" ]; then
    echo "export PATH=\"$HOME/.cargo/bin:\$PATH\""
  fi
}

# ---------- 体检 ----------
RESULT=()
record() { # name requirement actual source verdict
  RESULT+=("{\"tool\":\"$1\",\"require\":\"$2\",\"actual\":\"$3\",\"source\":\"$4\",\"ok\":$5}")
  row "$1" "$3" "$2" "$4" "$([ "$5" = true ] && echo '✓' || echo '✗')"
}

check_all() {
  printf '  %-8s %-10s %-12s %-10s %s\n' "工具" "实测" "要求" "来源" "结果"
  local v

  v="$(probe_gcc)"; if [ -n "$v" ] && ver_ge "$v" "$V_GCC_MIN"; then record gcc "$V_GCC_MIN" "$v" "$(src_of gcc)" true; else record gcc "$V_GCC_MIN" "${v:-缺}" "$([ -n "$v" ] && src_of gcc || echo none)" false; fi
  v="$(probe_cmake)"; if [ -n "$v" ] && ver_ge "$v" "$V_CMAKE_MIN"; then record cmake "$V_CMAKE_MIN" "$v" "$(src_of cmake)" true; else record cmake "$V_CMAKE_MIN" "${v:-缺}" "$([ -n "$v" ] && src_of cmake || echo none)" false; fi
  v="$(probe_ninja)"; if [ -n "$v" ] && ver_ge "$v" "$V_NINJA_MIN"; then record ninja "$V_NINJA_MIN" "$v" "$(src_of ninja)" true; else record ninja "$V_NINJA_MIN" "${v:-缺}" none false; fi
  v="$(probe_python)"; if [ -n "$v" ] && ver_ge "$v" "$V_PY_MIN"; then record python "$V_PY_MIN" "$v" "$(src_of python3)" true; else record python "$V_PY_MIN" "${v:-缺}" none false; fi

  v="$(probe_ccache)"; if [ -z "$v" ]; then
    record ccache "$V_CCACHE_MIN" "缺" none false
  elif [ -x "$PS_DEPS_ROOT/toolchain/ccache/bin/ccache" ]; then
    ver_ge "$v" "$V_CCACHE_MIN" && record ccache "$V_CCACHE_MIN" "$v" managed true || record ccache "$V_CCACHE_MIN" "$v" managed false
  else
    # 系统 ccache 只要版本达标即可用于构建，与是否固化到持久目录无关
    ver_ge "$v" "$V_CCACHE_MIN" && record ccache "$V_CCACHE_MIN" "$v" system true || record ccache "$V_CCACHE_MIN" "$v" system false
  fi

  v="$(probe_rust)"; if [ -z "$v" ]; then
    record rust "$V_RUST_REQ" "缺" none false
  else
    local src=system; [ -x "$PS_DEPS_ROOT/toolchain/rust/cargo/bin/cargo" ] && src=managed
    [ "$v" = "$V_RUST_REQ" ] && record rust "$V_RUST_REQ" "$v" "$src" true || record rust "$V_RUST_REQ" "$v" "$src" false
  fi

  local q; if q="$(find_qt)"; then record qt "$V_QT" "$V_QT" "$(qt_source)" true; else record qt "$V_QT" "缺" none false; fi
}

# ---------- shell 块 ----------
strip_block() {
  local f="$1"; [ -f "$f" ] || return 0
  awk -v b="$MARK_BEGIN" -v e="$MARK_END" '
    $0 == b { skip = 1; next }
    $0 == e { skip = 0; next }
    !skip { print }
  ' "$f" >"$f.$$.tmp" && mv "$f.$$.tmp" "$f"
}

write_block() {
  local qt="" f done_files=()
  qt="$(find_qt || true)"
  for f in "${STARTUP_FILES[@]}"; do
    if ! touch "$f" 2>/dev/null; then
      log "跳过 $f（不可写）"
      continue
    fi
    strip_block "$f"
    if ! {
      echo "$MARK_BEGIN"
      echo "export PS_DEPS_ROOT=\"$PS_DEPS_ROOT\""
      echo "export PATH=\"$PS_DEPS_ROOT/bin:$PS_DEPS_ROOT/toolchain/python/venv/bin:$PS_DEPS_ROOT/toolchain/ccache/bin:\$PATH\""
      [ -n "$qt" ] && {
        echo "export PS_QT_DIR=\"$qt\""
        echo 'export CMAKE_PREFIX_PATH="$PS_QT_DIR${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"'
        echo 'export LD_LIBRARY_PATH="$PS_QT_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"'
      }
      echo "export CCACHE_DIR=\"$PS_DEPS_ROOT/caches/ccache\""
      echo "export CCACHE_MAXSIZE=5G"
      echo "export CCACHE_BASEDIR=\"$ROOT\""
      rust_env
      echo "$MARK_END"
    } >>"$f" 2>/dev/null; then
      log "写入 $f 失败"
      continue
    fi
    done_files+=("$f")
  done
  if [ "${#done_files[@]}" -eq 0 ]; then
    log "shell 块未写入任何文件"
    return 1
  fi
  log "已写入 ${done_files[*]}"
}

print_env() {
  local qt; qt="$(find_qt || true)"
  echo "export PS_DEPS_ROOT=\"$PS_DEPS_ROOT\""
  echo "export PATH=\"$PS_DEPS_ROOT/bin:$PS_DEPS_ROOT/toolchain/python/venv/bin:$PS_DEPS_ROOT/toolchain/ccache/bin:\$PATH\""
  [ -n "$qt" ] && {
    echo "export PS_QT_DIR=\"$qt\""
    echo 'export CMAKE_PREFIX_PATH="$PS_QT_DIR${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"'
    echo 'export LD_LIBRARY_PATH="$PS_QT_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"'
  }
  echo "export CCACHE_DIR=\"$PS_DEPS_ROOT/caches/ccache\""
  echo "export CCACHE_MAXSIZE=5G"
  echo "export CCACHE_BASEDIR=\"$ROOT\""
  rust_env
}

# 让脚本自身（尤其结尾体检）看到刚装的东西，不必等用户开新终端
env_apply() { eval "$(print_env)"; }

save_state() {
  if ! mkdir -p "$STATE_DIR" 2>/dev/null; then
    log "状态目录不可写：$STATE_DIR（跳过快照）"
    return 1
  fi
  if printf '{"root":"%s","manifest":"%s","generated":"%s","tools":[%s]}\n' \
    "$PS_DEPS_ROOT" "$MANIFEST" "$(date -Iseconds)" "$(IFS=,; echo "${RESULT[*]:-}")" >"$STATE_FILE" 2>/dev/null; then
    log "状态写入 $STATE_FILE"
  else
    log "状态写入失败：$STATE_FILE"
    return 1
  fi
}

# ---------- 安装 ----------
apt_install() {
  local pkgs=("$@"); [ ${#pkgs[@]} -eq 0 ] && return 0
  log "apt 安装：${pkgs[*]}"
  sudo apt-get update -qq && sudo DEBIAN_FRONTEND=noninteractive apt-get install -y "${pkgs[@]}"
}

install_toolchain() {
  local pkgs=()

  # 旧布局遗留：bin/cmake 存在但 toolchain/cmake 还没有实体，补一层入口软链
  if [ -x "$PS_DEPS_ROOT/bin/cmake" ] && [ ! -e "$PS_DEPS_ROOT/toolchain/cmake/bin/cmake" ]; then
    mkdir -p "$PS_DEPS_ROOT/toolchain/cmake/bin"
    ln -sf "$(readlink -f "$PS_DEPS_ROOT/bin/cmake")" "$PS_DEPS_ROOT/toolchain/cmake/bin/cmake"
    ln -sf "$PS_DEPS_ROOT/toolchain/cmake/bin/cmake" "$PS_DEPS_ROOT/bin/cmake"
    log "cmake 入口归位：bin/cmake → toolchain/cmake/bin/cmake"
  fi

  # cmake：门槛 ≥3.24，apt 版多半不达标（Ubuntu 22.04 只到 3.22）。
  # 依次尝试：已有持久软链 → ~/.local/bin 达标版 → venv 里 pip 装官方 wheel（免 sudo、免镜像）→ apt
  if [ ! -x "$PS_DEPS_ROOT/bin/cmake" ]; then
    local alt="$HOME/.local/bin/cmake" pip="$PS_DEPS_ROOT/toolchain/python/venv/bin/pip"
    local entry="$PS_DEPS_ROOT/toolchain/cmake/bin/cmake"
    mkdir -p "$PS_DEPS_ROOT/bin" "$PS_DEPS_ROOT/toolchain/cmake/bin"
    if [ -x "$alt" ] && ver_ge "$("$alt" --version | head -1 | sed 's/cmake version //')" "$V_CMAKE_MIN"; then
      ln -sf "$alt" "$entry"
      ln -sf "$entry" "$PS_DEPS_ROOT/bin/cmake"
      log "cmake 软链：bin/cmake → toolchain/cmake/bin/cmake → $alt"
    elif [ -x "$pip" ] && "$pip" install --quiet --disable-pip-version-check cmake; then
      ln -sf "$PS_DEPS_ROOT/toolchain/python/venv/bin/cmake" "$entry"
      ln -sf "$entry" "$PS_DEPS_ROOT/bin/cmake"
      log "cmake 由 pip 装入 venv：bin/cmake → toolchain/cmake/bin/cmake → venv（$("$PS_DEPS_ROOT/bin/cmake" --version | head -1)）"
    elif [ -z "$(probe_cmake)" ]; then
      pkgs+=(cmake)
    else
      log "系统 cmake $(probe_cmake) 低于 $V_CMAKE_MIN，且 venv/pip 未就绪：先跑 --install --python，或自行安装 ≥$V_CMAKE_MIN 的 cmake"
    fi
  fi

  [ -z "$(probe_ninja)" ] && pkgs+=(ninja-build)
  [ -z "$(probe_ccache)" ] && pkgs+=(ccache)
  if [ -z "$(probe_gcc)" ]; then
    log "gcc 缺失：本脚本不自动安装（nphelper 暂无 gcc 镜像包），请执行："
    log "  sudo apt-get install -y build-essential"
  fi
  apt_install "${pkgs[@]}" || true
  [ -z "$(probe_ccache)" ] || [ -x "$PS_DEPS_ROOT/toolchain/ccache/bin/ccache" ] || make_ccache_selfcontained

  # 工具链步骤的成败只看 cmake 是否真可用（其余项已由体检兜底）
  if [ ! -x "$PS_DEPS_ROOT/bin/cmake" ] && ! { [ -n "$(probe_cmake)" ] && ver_ge "$(probe_cmake)" "$V_CMAKE_MIN"; }; then
    log "cmake 未就绪（需 ≥$V_CMAKE_MIN）"
    return 1
  fi
}

# ccache 落持久目录：环境重置会清 /usr/bin 及 apt 带来的 libzstd/libhiredis，故连库一起拷
make_ccache_selfcontained() {
  command -v ccache >/dev/null 2>&1 || { log "ccache 不可用，跳过固化"; return 1; }
  local src dst="$PS_DEPS_ROOT/toolchain/ccache"
  src="$(command -v ccache)"
  rm -rf "$dst"; mkdir -p "$dst/bin" "$dst/libexec" "$dst/lib"
  cp "$src" "$dst/libexec/ccache-real"
  local l base
  while IFS= read -r l; do
    base="$(basename "$l")"
    case "$base" in libc.so.* | libm.so.* | ld-linux*) continue ;; esac
    [ -f "$l" ] && cp -n "$l" "$dst/lib/" 2>/dev/null
  done < <(ldd "$src" | awk '/=> \//{print $3}')
  cat >"$dst/bin/ccache" <<'WRAPPER'
#!/bin/bash
SELF_DIR=$(cd "$(dirname "$0")" && pwd)
export LD_LIBRARY_PATH="$SELF_DIR/../lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec -a ccache "$SELF_DIR/../libexec/ccache-real" "$@"
WRAPPER
  chmod +x "$dst/bin/ccache" "$dst/libexec/ccache-real"
  log "ccache 固化到 $dst"
}

install_python() {
  mkdir -p "$PS_DEPS_ROOT"
  if [ ! -x "$PS_DEPS_ROOT/toolchain/python/venv/bin/python" ]; then
    log "创建 venv：$PS_DEPS_ROOT/toolchain/python/venv"
    local errf; errf="$(mktemp)"
    if ! python3 -m venv "$PS_DEPS_ROOT/toolchain/python/venv" 2>"$errf"; then
      rm -rf "$PS_DEPS_ROOT/toolchain/python/venv"
      # Ubuntu 默认不带 ensurepip（缺 python3-venv），有免密 sudo 时自动补齐后重试一次
      if command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
        log "venv 创建失败（多为缺 python3-venv），apt 补齐后重试"
        sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq python3-venv >/dev/null 2>&1 || true
        if ! python3 -m venv "$PS_DEPS_ROOT/toolchain/python/venv" 2>"$errf"; then
          cat "$errf" >&2
          rm -f "$errf"
          log "venv 仍创建失败：$PS_DEPS_ROOT/toolchain/python/venv"
          return 1
        fi
      else
        cat "$errf" >&2
        rm -f "$errf"
        rm -rf "$PS_DEPS_ROOT/toolchain/python/venv"
        log "venv 创建失败：请先 sudo apt-get install -y python3-venv，再重跑 --install --python"
        return 1
      fi
    fi
    rm -f "$errf"
  fi
  local req="$ROOT/${V_PY_REQ:-tools/requirements.txt}"
  if [ -f "$req" ]; then
    if "$PS_DEPS_ROOT/toolchain/python/venv/bin/pip" install --quiet --disable-pip-version-check -r "$req"; then
      log "pip 依赖已安装：$req"
    else
      log "pip 依赖安装失败：$req"
      return 1
    fi
  else
    log "缺 $req（versions.json → toolchains.python.configuration），跳过 pip"
  fi
}

install_qt() {
  if find_qt >/dev/null; then log "Qt $V_QT 已存在：$(find_qt)"; return 0; fi
  mkdir -p "$PS_DEPS_ROOT/toolchain/qt"

  local aqt="$PS_DEPS_ROOT/toolchain/python/venv/bin/aqt"
  if [ ! -x "$aqt" ]; then
    if "$PS_DEPS_ROOT/toolchain/python/venv/bin/pip" install --quiet --disable-pip-version-check aqtinstall 2>/dev/null; then
      log "aqtinstall 已装入 venv"
    else
      aqt="$(command -v aqt || true)"
    fi
  fi
  if [ ! -x "$aqt" ]; then
    log "缺 aqt：先跑 --install --python（venv 里装 aqtinstall）"
    return 1
  fi

  log "下载 Qt $V_QT 到 $PS_DEPS_ROOT/toolchain/qt（约 200MB）"
  "$aqt" install-qt linux desktop "$V_QT" "$QT_ABI" \
    -O "$PS_DEPS_ROOT/toolchain/qt" --archives qtbase qtsvg qttools icu || {
    log "Qt 下载失败（检查网络与 aqt 源）"
    return 1
  }
  if ! find_qt >/dev/null; then
    log "Qt 安装后仍找不到 $V_QT/$QT_ABI"
    return 1
  fi
  log "Qt 已安装：$(find_qt)"
}

install_rust() {
  if [ -n "$V_RUST_CHANNEL" ] && ! ver_ge "$V_RUST_CHANNEL" "$V_RUST_MIN"; then
    log "警告：$RUST_TOML 的 channel($V_RUST_CHANNEL) 低于 versions.json 门槛($V_RUST_MIN)"
  fi
  mkdir -p "$PS_DEPS_ROOT/toolchain/rust"
  export RUSTUP_HOME="$PS_DEPS_ROOT/toolchain/rust/rustup" CARGO_HOME="$PS_DEPS_ROOT/toolchain/rust/cargo"
  if [ ! -x "$CARGO_HOME/bin/cargo" ]; then
    log "安装 rustup + $V_RUST_REQ 到 $PS_DEPS_ROOT/toolchain/rust"
    curl -fsSL https://sh.rustup.rs | sh -s -- -y --no-modify-path --default-toolchain "$V_RUST_REQ" || return 1
  fi
  "$CARGO_HOME/bin/cargo" --version
}

# ---------- 主流程 ----------
case "$MODE" in
  --check)
    check_all
    save_state
    printf '\n'
    [ "${#RESULT[@]}" -gt 0 ] && printf '%s\n' "${RESULT[@]}" | grep -q '"ok":false' && exit 1 || exit 0
    ;;
  --print-env) print_env ;;
  --uninstall)
    for f in "${STARTUP_FILES[@]}"; do strip_block "$f" && log "已清理 $f"; done
    log "已装目录保留在 $PS_DEPS_ROOT"
    ;;
  --install)
    mkdir -p "$PS_DEPS_ROOT/bin" "$PS_DEPS_ROOT/caches/ccache"
    fails=0
    [ -n "$V_RUST_CHANNEL" ] && ! ver_ge "$V_RUST_CHANNEL" "$V_RUST_MIN" && log "警告：$RUST_TOML channel($V_RUST_CHANNEL) < versions.json 门槛($V_RUST_MIN)"
    if [ "$DO_PYTHON" = 1 ]; then install_python || fails=$((fails + 1)); fi
    if [ "$DO_TOOLCHAIN" = 1 ]; then install_toolchain || fails=$((fails + 1)); fi
    if [ "$DO_RUST" = 1 ]; then install_rust || fails=$((fails + 1)); fi
    if [ "$DO_QT" = 1 ]; then install_qt || fails=$((fails + 1)); fi
    write_block
    env_apply
    check_all
    save_state
    printf '\n'
    if [ "$fails" -ne 0 ]; then
      log "完成，但有 $fails 个步骤失败：按上表与上方提示补齐后重跑 --install"
      exit 1
    fi
    log "完成。新终端生效；当前终端可： source ~/.bashrc"
    exit 0
    ;;
esac
