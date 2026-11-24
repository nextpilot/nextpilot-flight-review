#!/usr/bin/env bash
# PlotScope 编译环境统一入口（Ubuntu/Debian）。
#
#   tools/ubuntu-setup.sh --check                 # 只体检，不改任何文件（默认）
#   tools/ubuntu-setup.sh --install               # 全量：工具链 + Qt + Rust + Python venv + shell 块
#   tools/ubuntu-setup.sh --install --toolchain   # 只处理 cmake / ninja / ccache（gcc 只体检，不自动装）
#   tools/ubuntu-setup.sh --install --qt          # 只处理 Qt
#   tools/ubuntu-setup.sh --install --python      # 只处理 venv 与 pip 依赖
#   tools/ubuntu-setup.sh --install --rust        # 只处理 rustup 工具链
#   tools/ubuntu-setup.sh --warm-cache            # 用 preset 预热 ccache：Release + Debug 各跑一次全量
#   tools/ubuntu-setup.sh --warm-cache --force    # 已达标也强制重跑
#   tools/ubuntu-setup.sh --install --warm-cache  # 装环境时顺手预热
#   tools/ubuntu-setup.sh --print-env             # 输出可 eval 的环境（CI 用）
#   tools/ubuntu-setup.sh --uninstall             # 移除写进 shell 的块（保留已装目录）
#
# 镜像：每个工具的下载地址写在 deps-version.json → toolchains.<工具>.mirrors，
#   每个工具给一组 URL（国内在前、官方兜底），脚本按顺序轮询，第一个可达的即用，
#   避免国内网络下官方源超时卡死。换源只改 deps-version.json，脚本无需改。
#
# 原则：系统自带且版本达标就用系统，不达标统一装进 $PS_DEPS_ROOT；版本只从
# 仓库根 deps-version.json 读，本脚本不写死任何版本号。
set -uo pipefail

if [ ! -f /etc/debian_version ] || [ "$(uname -s)" != "Linux" ]; then
  echo "[ubuntu-setup] 本脚本只支持 Ubuntu/Debian；Windows 见 docs/develop/building.md" >&2
  exit 2
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export PS_DEPS_ROOT="${PS_DEPS_ROOT:-$HOME/.ps-deps}"
MANIFEST="$ROOT/deps-version.json"
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
DO_WARM=0
DO_FORCE=0
for arg in "$@"; do
  case "$arg" in
    --check | --install | --print-env | --uninstall) MODE="$arg" ;;
    --warm-cache) DO_WARM=1 ;;
    --force) DO_FORCE=1 ;;
    --toolchain) DO_QT=0; DO_PYTHON=0; DO_RUST=0 ;;
    --qt) DO_TOOLCHAIN=0; DO_PYTHON=0; DO_RUST=0 ;;
    --python) DO_TOOLCHAIN=0; DO_QT=0; DO_RUST=0 ;;
    --rust) DO_TOOLCHAIN=0; DO_QT=0; DO_PYTHON=0 ;;
    -h | --help) sed -n '2,21p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "未知参数：$arg" >&2; exit 2 ;;
  esac
done
[ "$DO_WARM" = 1 ] && [ "$MODE" = "--check" ] && MODE="--warm-cache"

log() { printf '[ubuntu-setup] %s\n' "$*"; }
row() { printf '  %-8s %-10s %-12s %-10s %s\n' "$1" "$2" "$3" "$4" "$5"; }

# ---------- deps-version.json ----------
if [ ! -f "$MANIFEST" ]; then
  log "缺少 $MANIFEST"
  exit 1
fi
if ! python3 -m json.tool "$MANIFEST" >/dev/null 2>&1; then
  log "deps-version.json 语法错误：$MANIFEST"
  exit 1
fi

json_get() { # json_get qt version / json_get toolchain gcc min —— 逐层取键
  python3 -c 'import json,sys
d=json.load(open(sys.argv[1]))
for k in sys.argv[2:]:
    d=d[k]
print(d)' "$MANIFEST" "$@" 2>/dev/null
}

ver_ge() { [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -1)" = "$2" ]; }

# 版本要求写成带运算符的表达式（见 deps-version.json → toolchains.*.version）：
#   ">=3.24" 最低版本   "=6.2.4" 精确版本
# ver_split 拆成 <运算符>TAB<版本号>；无运算符时默认 >=。
ver_split() {
  local expr="$1" op=">=" num="$1"
  case "$expr" in
    ">="*) op=">="; num="${expr#>=}" ;;
    "=="*) op="=";  num="${expr#==}" ;;
    "="*)  op="=";  num="${expr#=}" ;;
  esac
  num="$(printf '%s' "$num" | tr -d '[:space:]')"
  printf '%s\t%s\n' "$op" "$num"
}

# ver_satisfy <实测版本> <表达式>：按表达式里的运算符判断是否满足。
# 例：ver_satisfy 3.28.3 ">=3.24" → true；ver_satisfy 6.4.0 "=6.2.4" → false
ver_satisfy() {
  local actual="$1" expr="$2" op num
  [ -z "$actual" ] && return 1
  IFS=$'\t' read -r op num < <(ver_split "$expr")
  case "$op" in
    "=")  [ "$actual" = "$num" ] ;;
    *)    ver_ge "$actual" "$num" ;;   # >= 及无运算符默认
  esac
}

# 读版本表达式（*_VER，体检判断用）并拆出纯版本号（*_MIN，路径/文案/fallback 用）
V_QT_VER="$(json_get toolchains qt version)"
IFS=$'\t' read -r _qt_op V_QT < <(ver_split "$V_QT_VER")
QT_ABI="$(json_get toolchains qt abi)"
V_GCC_VER="$(json_get toolchains gcc version)";        IFS=$'\t' read -r _o V_GCC_MIN < <(ver_split "$V_GCC_VER")
V_CMAKE_VER="$(json_get toolchains cmake version)";    IFS=$'\t' read -r _o V_CMAKE_MIN < <(ver_split "$V_CMAKE_VER")
V_NINJA_VER="$(json_get toolchains ninja version)";    IFS=$'\t' read -r _o V_NINJA_MIN < <(ver_split "$V_NINJA_VER")
V_CCACHE_VER="$(json_get toolchains ccache version)";  IFS=$'\t' read -r _o V_CCACHE_MIN < <(ver_split "$V_CCACHE_VER")
V_PY_VER="$(json_get toolchains python version)";      IFS=$'\t' read -r _o V_PY_MIN < <(ver_split "$V_PY_VER")
V_PY_REQ="$(json_get toolchains python configuration)"

# Rust 的实际版本写在 rust-toolchain.toml（cargo 直读），deps-version.json 只给门槛与文件路径
V_RUST_VER="$(json_get toolchains rust version)"; IFS=$'\t' read -r _o V_RUST_MIN < <(ver_split "$V_RUST_VER")
V_RUST_CFG="$(json_get toolchains rust configuration)"
RUST_TOML="$ROOT/${V_RUST_CFG:-src/kernel/rust-toolchain.toml}"
V_RUST_CHANNEL="$([ -f "$RUST_TOML" ] && sed -n 's/^channel *= *"\(.*\)"/\1/p' "$RUST_TOML" | head -1)"
V_RUST_REQ="${V_RUST_CHANNEL:-$V_RUST_MIN}"

# ---------- 镜像轮询 ----------
# deps-version.json → toolchains.<工具>.mirrors：每个工具给一组 URL（国内在前、官方兜底）。
# 脚本按顺序逐个探测，第一个可达的即用；全不通则回显第一个，让安装阶段报真实错误。
# 换源只改 deps-version.json，脚本无需改；某工具没配 mirrors 时回显空（调用方沿用原行为）。

# 探测一个 URL 在 6s 内是否可达；只关心连通性，不下载正文。
reachable() {
  curl -fsS -o /dev/null --connect-timeout 3 --max-time 6 "$1" >/dev/null 2>&1
}

# json_array <键...>：把 JSON 数组逐行输出（元素为字符串时原样输出）。
json_array() {
  python3 -c 'import json,sys
d=json.load(open(sys.argv[1]))
for k in sys.argv[2:]: d=d[k]
for x in d: print(x)' "$MANIFEST" "$@" 2>/dev/null || true
}

# pick_url <工具名> <探测路径>：轮询 toolchains.<工具名>.mirrors 的字符串数组，
# 对每个 base 拼探测路径测连通性，回显第一个可达的 base；空配置回显空。
pick_url() {
  local tool="$1" probe_path="${2:-/}" url first=""
  while IFS= read -r url; do
    [ -z "$url" ] && continue
    [ -z "$first" ] && first="$url"
    if reachable "${url%/}${probe_path}"; then echo "$url"; return 0; fi
  done < <(json_array toolchains "$tool" mirrors)
  echo "$first"
}

# pick_rustup：轮询 toolchains.rust.mirrors 的对象数组，对每个 installer 探测，
# 回显第一个可达条目的 installer<TAB>dist_server<TAB>update_root。
pick_rustup() {
  local installer dist update first_i="" first_d="" first_u=""
  local row
  while IFS= read -r row; do
    [ -z "$row" ] && continue
    installer="$(printf '%s' "$row" | python3 -c 'import json,sys;print(json.load(sys.stdin).get("installer",""))' 2>/dev/null)"
    dist="$(printf '%s' "$row" | python3 -c 'import json,sys;print(json.load(sys.stdin).get("dist_server",""))' 2>/dev/null)"
    update="$(printf '%s' "$row" | python3 -c 'import json,sys;print(json.load(sys.stdin).get("update_root",""))' 2>/dev/null)"
    [ -z "$first_i" ] && { first_i="$installer"; first_d="$dist"; first_u="$update"; }
    if [ -n "$installer" ] && reachable "$installer"; then
      printf '%s\t%s\t%s\n' "$installer" "$dist" "$update"; return 0
    fi
  done < <(python3 -c 'import json,sys
d=json.load(open(sys.argv[1]))["toolchains"]["rust"]["mirrors"]
for x in d: print(json.dumps(x,ensure_ascii=False))' "$MANIFEST" 2>/dev/null)
  [ -n "$first_i" ] && printf '%s\t%s\t%s\n' "$first_i" "$first_d" "$first_u"
}

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

  v="$(probe_gcc)"; if ver_satisfy "$v" "$V_GCC_VER"; then record gcc "$V_GCC_VER" "$v" "$(src_of gcc)" true; else record gcc "$V_GCC_VER" "${v:-缺}" "$([ -n "$v" ] && src_of gcc || echo none)" false; fi
  v="$(probe_cmake)"; if ver_satisfy "$v" "$V_CMAKE_VER"; then record cmake "$V_CMAKE_VER" "$v" "$(src_of cmake)" true; else record cmake "$V_CMAKE_VER" "${v:-缺}" "$([ -n "$v" ] && src_of cmake || echo none)" false; fi
  v="$(probe_ninja)"; if ver_satisfy "$v" "$V_NINJA_VER"; then record ninja "$V_NINJA_VER" "$v" "$(src_of ninja)" true; else record ninja "$V_NINJA_VER" "${v:-缺}" none false; fi
  v="$(probe_python)"; if ver_satisfy "$v" "$V_PY_VER"; then record python "$V_PY_VER" "$v" "$(src_of python3)" true; else record python "$V_PY_VER" "${v:-缺}" none false; fi

  v="$(probe_ccache)"; if [ -z "$v" ]; then
    record ccache "$V_CCACHE_MIN" "缺" none false
  elif [ -x "$PS_DEPS_ROOT/toolchain/ccache/bin/ccache" ]; then
    ver_satisfy "$v" "$V_CCACHE_VER" && record ccache "$V_CCACHE_VER" "$v" managed true || record ccache "$V_CCACHE_VER" "$v" managed false
  else
    # 系统 ccache 只要版本达标即可用于构建，与是否固化到持久目录无关
    ver_satisfy "$v" "$V_CCACHE_VER" && record ccache "$V_CCACHE_VER" "$v" system true || record ccache "$V_CCACHE_VER" "$v" system false
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
      # 分层：这里只放缓存物理位置；策略项进 ccache.conf，base_dir 进 preset。
      # 不再 export CCACHE_MAXSIZE/BASEDIR —— 环境变量优先级高于 conf/preset，
      # 会让未 source 脚本的 IDE/CI 与命令行行为不一致。
      echo "export CCACHE_DIR=\"$PS_DEPS_ROOT/caches/ccache\""
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
# apt 临时源：轮询 deps-version.json → toolchains.apt.mirrors，选第一个可达的镜像生成临时
# sources.list，仅本次命令生效，不改系统 /etc/apt 配置。
# 只对 Ubuntu 生效（Debian 的源结构与包名不同，禁止套用 Ubuntu 镜像源污染系统）。
apt_mirror_opts() {
  local codename keyring srcfile base
  APT_OPTS=()
  . /etc/os-release 2>/dev/null || true
  if [ "${ID:-}" != ubuntu ]; then
    log "当前非 Ubuntu（${ID:-unknown}），跳过 apt 镜像轮询，沿用系统源"
    return 0
  fi
  codename="${UBUNTU_CODENAME:-${VERSION_CODENAME:-}}"
  base="$(pick_url apt "/dists/${UBUNTU_CODENAME:-${VERSION_CODENAME:-}}/Release")"
  keyring=""
  [ -r /usr/share/keyrings/ubuntu-archive-keyring.gpg ] && keyring=/usr/share/keyrings/ubuntu-archive-keyring.gpg
  if [ -z "$codename" ] || [ -z "$base" ] || [ -z "$keyring" ]; then
    log "apt 镜像信息不全（codename=$codename base=$base keyring=$keyring），沿用系统源"
    return 0
  fi
  mkdir -p "$PS_DEPS_ROOT/caches"
  srcfile="$PS_DEPS_ROOT/caches/apt-sources-mirror.list"
  {
    echo "deb [signed-by=$keyring] $base $codename main restricted universe multiverse"
    echo "deb [signed-by=$keyring] $base ${codename}-updates main restricted universe multiverse"
    echo "deb [signed-by=$keyring] $base ${codename}-security main restricted universe multiverse"
    echo "deb [signed-by=$keyring] $base ${codename}-backports main restricted universe multiverse"
  } >"$srcfile"
  APT_OPTS=(-o "Dir::Etc::sourcelist=$srcfile" -o Dir::Etc::sourceparts=- -o APT::Get::List-Cleanup=0)
  log "apt 轮询选源：$base（codename=$codename）"
}
APT_OPTS=()

apt_install() {
  local pkgs=("$@"); [ ${#pkgs[@]} -eq 0 ] && return 0
  log "apt 安装：${pkgs[*]}"
  apt_mirror_opts
  sudo apt-get "${APT_OPTS[@]}" update -qq && \
    sudo DEBIAN_FRONTEND=noninteractive apt-get "${APT_OPTS[@]}" install -y "${pkgs[@]}"
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
    if [ -x "$alt" ] && ver_satisfy "$("$alt" --version | head -1 | sed 's/cmake version //')" "$V_CMAKE_VER"; then
      ln -sf "$alt" "$entry"
      ln -sf "$entry" "$PS_DEPS_ROOT/bin/cmake"
      log "cmake 软链：bin/cmake → toolchain/cmake/bin/cmake → $alt"
    elif [ -x "$pip" ] && pip_install "$pip" cmake; then
      ln -sf "$PS_DEPS_ROOT/toolchain/python/venv/bin/cmake" "$entry"
      ln -sf "$entry" "$PS_DEPS_ROOT/bin/cmake"
      log "cmake 由 pip 装入 venv：bin/cmake → toolchain/cmake/bin/cmake → venv（$("$PS_DEPS_ROOT/bin/cmake" --version | head -1)）"
    elif [ -z "$(probe_cmake)" ]; then
      pkgs+=(cmake)
    else
      # 走到这里说明：无持久软链、~/.local 不达标、pip 也失败，但系统有 cmake。
      # 系统版本可能达标（如 3.28.3 ≥ 3.24），此时只是「pip 没备好」，不是版本低，
      # 系统 cmake 已足以构建；只有确实低于门槛才需要升级。切勿一律报「低于」。
      if ver_satisfy "$(probe_cmake)" "$V_CMAKE_VER"; then
        log "cmake 由系统提供 $(probe_cmake)（≥$V_CMAKE_MIN，够用）；pip 托管的 cmake 未就绪，可先跑 --install --python 统一版本"
      else
        log "系统 cmake $(probe_cmake) 低于 $V_CMAKE_MIN，且 venv/pip 未就绪：先跑 --install --python，或自行安装 ≥$V_CMAKE_MIN 的 cmake"
      fi
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
  if [ ! -x "$PS_DEPS_ROOT/bin/cmake" ] && ! ver_satisfy "$(probe_cmake)" "$V_CMAKE_VER"; then
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


# ccache 预热：跑一次全量构建，把编译结果填进 ccache。
#
# 为什么值得单独做：冷构建约 90~210s（5 核实测，取决于 PCH 开关），
# 缓存命中后同样的全量重建能降到个位数秒级。换机器、清缓存目录、
# CI 冷 runner 都会撞上未命中那一条，预热一次把缓存填满即可。
#
# 幂等：缓存已达阈值时直接跳过（--force 可强制重跑）。阈值取 deps-version.json
# 里 toolchains.ccache.warm_min_mb，避免每次 --install 都白跑几分钟。
ccache_bin() {
  if [ -x "$PS_DEPS_ROOT/toolchain/ccache/bin/ccache" ]; then
    echo "$PS_DEPS_ROOT/toolchain/ccache/bin/ccache"
  else
    command -v ccache 2>/dev/null || true
  fi
}

# 把纯策略写进 $CCACHE_DIR/ccache.conf（与路径无关、持久、所有入口共享）。
# 与 shell 环境变量的分工：
#   - ccache.conf：max_size / hash_dir / depend_mode / sloppiness —— 纯策略，
#     团队一致、CI 也认；ccache 不依赖 shell 是否 source 过脚本。
#   - shell(CCACHE_DIR)：只在 .bashrc / --print-env 暴露缓存物理位置，
#     让手动 `ccache -s` 能读到同一份缓存。
#   - CMakePresets.json 的 environment(CCACHE_BASEDIR)：与仓库绝对路径绑定，
#     随仓库版本化，IDE / CI / cmake --preset 都会读。
# 关键点：conf 的优先级低于环境变量。所以 shell 里绝不能再 export 这些策略，
# 否则会覆盖 conf，导致"未 source 脚本的入口"和"source 过的入口"行为不一致。
write_ccache_conf() {
  local dir="${CCACHE_DIR:-$PS_DEPS_ROOT/caches/ccache}"
  local conf="$dir/ccache.conf"
  mkdir -p "$dir" || return 1
  cat >"$conf" <<'EOF'
# 由 tools/ubuntu-setup.sh 生成；纯策略，与路径无关。
# 改这里对命令行 / IDE / CI 同时生效（不要用 shell export 覆盖）。
max_size = 5G
# depend mode：用编译器 -MD 依赖信息判命中，跳过完整预处理（实测命中路径 90ms→65ms）。
# GCC 支持；CMake 默认给 CXX 加 -MD -MT，无需额外参数。
depend_mode = true
# hash_dir=false：工作目录不进哈希。仅当源码用相对路径的 __FILE__ 时才有意义，
# 本项目不用 __FILE__（已核查），设为 false 可让不同 CWD 的构建共享缓存。
hash_dir = false
# PCH 共存所需；源码不用 __TIME__/__DATE__，这两项无正确性风险。
# 切勿加 include_file_mtime/ctime：会让改过头文件的 TU 命中旧缓存、编出过期代码。
sloppiness = pch_defines,time_macros
# 缓存压缩，省磁盘；ccache 4.x 默认已开，显式声明以求各版本一致。
compression = true
compression_level = 5
EOF
  log "已写入 ccache 策略 $conf"
}

# 缓存实际占用（MB）。优先用 ccache --print-stats 的 cache_size_kibibyte（机器可读，
# 版本间格式稳定）；旧版 ccache 没有该选项时退化为量目录大小。
ccache_cache_mb() {
  local d="${CCACHE_DIR:-$PS_DEPS_ROOT/caches/ccache}"
  local cc; cc="$(ccache_bin)"
  if [ -n "$cc" ]; then
    local kib
    kib="$("$cc" --print-stats 2>/dev/null | awk '$1=="cache_size_kibibyte"{print $2}')"
    if [ -n "$kib" ]; then echo $((kib / 1024)); return 0; fi
  fi
  [ -d "$d" ] || { echo 0; return 0; }
  du -sm "$d" 2>/dev/null | awk '{print $1+0}'
}

# 预热状态标记：每个 preset 一份，跑完一次成功的预热才写。
#
# 为什么不能只看缓存大小：缓存目录是全局共享的，别人（或别的项目）留下的
# 几 GB 缓存也会让 size 达标，但里面未必有本仓库的条目——那种情况下日常构建
# 照样 miss，提示就失效了。标记文件绑定到本仓库的源码版本，才算数。
#
# 为什么要按 preset 分：Release(-O2) 和 Debug(-g) 是两套不同的编译命令，
# ccache 哈希不同。只暖 build/ 不代表 build-debug/ 也暖，必须分别记录。
WARM_PRESETS=("default" "debug")   # 与 CMakePresets.json 的 configurePresets 一致
warm_stamp() { echo "$STATE_DIR/ccache-warm.$1.stamp"; }
warm_stamp_valid() {
  local p
  for p in "${WARM_PRESETS[@]}"; do
    local f; f="$(warm_stamp "$p")"
    [ -f "$f" ] || return 1
    local want cur
    want="$(head -1 "$f" 2>/dev/null)"
    cur="$(ps_source_rev)"
    [ -n "$want" ] && [ "$want" = "$cur" ] || return 1
  done
}

# 仓库当前版本标识。
#
# 不能只用 git HEAD：未提交的改动（正在写一半的代码）不会体现在 HEAD 里，
# 那样"改完代码 → 缓存该失效"的判定就漏了。所以用 HEAD + 工作区差异 +
# 未跟踪文件列表三者一起算指纹，任何一处变了标记就失效。
ps_source_rev() {
  # ccache 配置也进指纹：conf 或 preset 的 base_dir / depend_mode / hash_dir 一变，
  # 哈希键就变、旧缓存作废。若指纹不含配置，改完配置后标记仍有效会跳过预热，
  # 导致用旧缓存key全 miss 却不自知。
  local cfg_fp
  cfg_fp="$(
    { cat "${CCACHE_DIR:-$PS_DEPS_ROOT/caches/ccache}/ccache.conf" 2>/dev/null
      grep -E 'CCACHE_(BASEDIR|DEPEND|NOHASHDIR|SLOPPINESS|MAXSIZE)' "$ROOT/CMakePresets.json" 2>/dev/null
    } | git hash-object --stdin 2>/dev/null || echo noconf
  )"
  if command -v git >/dev/null 2>&1 && [ -d "$ROOT/.git" ]; then
    local head diff untracked
    head="$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || true)"
    diff="$(git -C "$ROOT" diff HEAD 2>/dev/null | git hash-object --stdin 2>/dev/null || true)"
    untracked="$(git -C "$ROOT" ls-files --others --exclude-standard 2>/dev/null | sort | git hash-object --stdin 2>/dev/null || true)"
    if [ -n "$head" ]; then
      echo "git:${head}:${diff:-nodiff}:${untracked:-none}:cfg-${cfg_fp:-none}"
      return 0
    fi
  fi
  # 无 git：取所有 CMakeLists / cmake 文件的最新 mtime，作为粗粒度版本号
  echo "mtime:$(find "$ROOT/src" "$ROOT/cmake" -name 'CMakeLists.txt' -o -name '*.cmake' 2>/dev/null \
    | xargs stat -c %Y 2>/dev/null | sort -n | tail -1)"
}

# 累计命中/未命中（自上次 ccache -z 起），用于回答"这轮到底编了几个文件"
#
# pch_uncacheable 是 PCH 与 ccache 打架的探针：只要它不为 0，就说明有 TU
# 因为用了预编译头而完全绕过了缓存（ccache 手册「Precompiled headers」：
# 需要 sloppiness 含 pch_defines,time_macros）。此时缓存看着在涨，实际全在
# 重新编译。
ccache_counters() {
  local cc; cc="$(ccache_bin)"
  [ -n "$cc" ] || return 1
  "$cc" --print-stats 2>/dev/null | awk '
    $1=="direct_cache_hit"{h+=$2}
    $1=="preprocessed_cache_hit"{h+=$2}
    $1=="direct_cache_miss"{m+=$2}
    $1=="preprocessed_cache_miss"{m+=$2}
    $1=="could_not_use_precompiled_header"{p+=$2}
    $1=="local_storage_hit"{lh+=$2}
    $1=="local_storage_miss"{lm+=$2}
    END{printf "hits=%d misses=%d storage_hit=%d storage_miss=%d pch_uncacheable=%d\n",h,m,lh,lm,p}'
}

# PCH 与 ccache 是否处于"打架"状态（缓存存在但命不中）
ccache_pch_conflict() {
  local c; c="$(ccache_counters 2>/dev/null || true)"
  case "$c" in *"pch_uncacheable=0"|*"") return 1 ;; *) return 0 ;; esac
}

cache_is_warm() {
  local mb; mb="$(ccache_cache_mb)"
  [ "${mb:-0}" -ge "${V_CCACHE_WARM_MB:-12}" ] && warm_stamp_valid
}

warm_cache() {
  local cc; cc="$(ccache_bin)"
  if [ -z "$cc" ]; then
    log "错误：找不到 ccache，先跑 --install --toolchain"
    return 1
  fi
  export CCACHE_DIR="${CCACHE_DIR:-$PS_DEPS_ROOT/caches/ccache}"
  export PATH="$PS_DEPS_ROOT/toolchain/ccache/bin:$PATH"
  # 策略统一由 ccache.conf 提供（不在此处 export，避免覆盖 conf）。
  # base_dir 由 CMakePresets.json 的 environment 注入。
  write_ccache_conf

  local mb; mb="$(ccache_cache_mb)"
  if [ "$DO_FORCE" != 1 ] && cache_is_warm; then
    log "ccache 已预热（Release+Debug 两套都达标，且与当前源码版本匹配），跳过。"
    log "  需要强制重跑：tools/ubuntu-setup.sh --warm-cache --force"
    return 0
  fi

  local qt qtlib
  if ! qt="$(find_qt)"; then
    log "错误：找不到 Qt $V_QT，预热构建无法 configure。"
    log "  已知布局：$PS_DEPS_ROOT/toolchain/qt/$V_QT/$QT_ABI、$ROOT/deps/Qt/$V_QT/$QT_ABI、\$PS_DEPS_PREFIX/usr/lib/*/cmake/Qt6"
    log "  若 Qt 装在别处，先 export PS_DEPS_PREFIX=<前缀> 再重试。"
    return 1
  fi
  qtlib="$(qt_libdir || true)"
  # preset 里 CMAKE_PREFIX_PATH=$env{PS_QT_DIR}，必须 export 这个变量，
  # 否则 preset configure 时 CMAKE_PREFIX_PATH 为空、找不到 Qt。
  export PS_QT_DIR="$qt"
  export CMAKE_PREFIX_PATH="$qt${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
  [ -n "$qtlib" ] && export LD_LIBRARY_PATH="$qtlib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

  # 逐 preset 预热。关键约束（实测教训，别改）：
  #   1) 必须用 preset itself 来 configure+build，不能自己拼 -G / -DCMAKE_BUILD_TYPE。
  #      preset 里锁定了生成器（Unix Makefiles）与全部参数，手拼会与日常构建不一致——
  #      生成器不同会让编译命令里的路径/参数不同，ccache 哈希对不上，预热白跑。
  #   2) build 目录必须是 preset 的 binaryDir（default→build，debug→build-debug），
  #      绝不能另开 build-warm/。ccache 哈希含 -I/-o 的绝对路径，换目录就 miss；
  #      CCACHE_BASEDIR / hash_dir=false 都只归一化工作目录，救不了 -I/-o 里的路径。
  #   3) Release(-O2) 与 Debug(-g) 是两套哈希，必须分别预热，否则只暖一套。
  local p t0 t1 bd cfglog buildlog
  for p in "${WARM_PRESETS[@]}"; do
    case "$p" in
      default) bd="$ROOT/build" ;;
      debug)   bd="$ROOT/build-debug" ;;
      *)       bd="$ROOT/build-$p" ;;
    esac
    mkdir -p "$bd"
    cfglog="/tmp/ps-warm-$p-cfg.log"
    buildlog="/tmp/ps-warm-$p-build.log"
    log "预热 [$p]：cmake --preset $p → $bd"
    t0="$(date +%s)"
    # shellcheck disable=SC2086
    if ! cmake --preset "$p" >"$cfglog" 2>&1; then
      # 最常见的失败：build 目录是上次用 -G Ninja 手拼的，preset 要 Unix Makefiles，
      # 生成器不匹配。给出可操作的修法，而不是甩 CMake 原始报错。
      if grep -q "Does not match the generator used previously" "$cfglog"; then
        local oldgen
        oldgen="$(sed -n 's/.*previously: //p' "$cfglog" | head -1)"
        log "错误：[$p] build 目录 $bd 的生成器是 [$oldgen]，但 preset 要求 [Unix Makefiles]。"
        log "      两者不一致会导致 ccache 哈希对不上、预热白跑。"
        log "      修法：rm -rf $bd 后重跑本命令（build 目录可安全重建）。"
      else
        log "错误：[$p] configure 失败，日志 $cfglog"
        tail -5 "$cfglog" | sed 's/^/    /'
      fi
      return 1
    fi
    if ! cmake --build --preset "$p" -j"$(nproc 2>/dev/null || echo 2)" >"$buildlog" 2>&1; then
      log "错误：[$p] 构建失败，日志 $buildlog"
      tail -5 "$buildlog" | sed 's/^/    /'
      return 1
    fi
    t1="$(date +%s)"
    log "  [$p] 完成，用时 $((t1 - t0))s"
    # 每个 preset 成功才写自己的标记：中途失败不会留下"已预热"的假象。
    mkdir -p "$STATE_DIR"
    ps_source_rev >"$(warm_stamp "$p")"
  done

  mb="$(ccache_cache_mb)"
  log "预热完成，两套（Release+Debug）缓存共 ${mb}MB"
  "$cc" -s 2>/dev/null | sed 's/^/  /'
  local c; c="$(ccache_counters || true)"
  [ -n "$c" ] && log "  累计计数（自上次 ccache -z 起）：$c"
  if ccache_pch_conflict; then
    log "警告：存在 pch_uncacheable，PCH 与 ccache 正在互相拖垮。"
    log "      请确认 CCACHE_SLOPPINESS 含 pch_defines,time_macros。"
  fi
  log "已记录预热标记：$(warm_stamp default) / $(warm_stamp debug)"
  if [ "${mb:-0}" -lt "${V_CCACHE_WARM_MB:-12}" ]; then
    log "提示：缓存 ${mb}MB 小于阈值 ${V_CCACHE_WARM_MB:-12}MB（deps-version.json → toolchains.ccache.warm_min_mb）。"
  fi
  return 0
}

# pip 安装的自适应包装：应对「企业内网/沙箱 HTTPS 代理注入自签根 CA，venv 自带
# certifi 不认、导致所有源都报 CERTIFICATE_VERIFY_FAILED」的环境。按代价从小到大
# 依次尝试，任何一档成功即返回；完全正常的机器第一档就过，零额外开销：
#   1) 原样安装（默认，绝大多数机器走这里）；
#   2) 仅当错误确为证书问题时，改用系统 CA bundle（/etc/ssl/certs/ca-certificates.crt，
#      已含本地注入的根 CA）——保留证书校验，安全；
#   3) 仍失败才降级到 --trusted-host（关闭校验），且只信任当前 index 的 host，
#      不引入新的 host、不放宽其他源；并用 PIP_PIP_INSECURE=1 显式记录这次降级。
# 用法：pip_install <pip绝对路径> <pip install 的其余参数...>
PIP_CA=""  # 惰性探测，系统 CA bundle 存在才用
PIP_INDEX_OPT=()  # 惰性：轮询 toolchains.python.mirrors 选第一个可达 index；用户已显式配源则不注入
PIP_INDEX_DONE=0
pip_index_setup() {
  [ "$PIP_INDEX_DONE" = 1 ] && return 0
  PIP_INDEX_DONE=1
  local url configured
  # 尊重用户已有的显式 index（企业内网私有源优先），不覆盖
  configured="$("$1" config list 2>/dev/null | grep -E '^global\.index-url=|^global\.extra-index-url=' || true)"
  if [ -n "$configured" ]; then
    log "pip 已有显式 index 配置，跳过镜像轮询"
    return 0
  fi
  url="$(pick_url python /)"
  [ -z "$url" ] && return 0
  PIP_INDEX_OPT=(--index-url "$url")
  log "pip 轮询选源：$url"
}
pip_install() {
  local pip="$1"; shift
  pip_index_setup "$pip"
  local err; err="$(mktemp)"
  if "$pip" install --quiet --disable-pip-version-check "${PIP_INDEX_OPT[@]}" "$@" 2>"$err"; then
    rm -f "$err"; return 0
  fi
  # 只在确认是证书校验问题时才降级，网络超时/404 等原样报错，不掩盖真实故障
  if grep -qiE 'CERTIFICATE_VERIFY_FAILED|certificate verify failed|unable to get local issuer certificate' "$err"; then
    if [ -z "$PIP_CA" ]; then
      local ca="/etc/ssl/certs/ca-certificates.crt"
      [ -r "$ca" ] && PIP_CA="$ca"
    fi
    if [ -n "$PIP_CA" ]; then
      log "pip 证书校验失败，尝试系统 CA bundle：$PIP_CA（保留校验）"
      : >"$err"
      if "$pip" install --quiet --disable-pip-version-check "${PIP_INDEX_OPT[@]}" --cert "$PIP_CA" "$@" 2>"$err"; then
        rm -f "$err"; return 0
      fi
    fi
    # 最后手段：仅信任当前 index 的 host，不改全局、不新增来源
    local host
    host="$("$pip" config list 2>/dev/null | sed -n 's/^global.index-url=.*:\/\/\([^/]*\).*/\1/p' | head -1)"
    # 注入了国内 index 时，config list 看不到它，直接从注入的 URL 取 host
    if [ -z "$host" ] && [ "${#PIP_INDEX_OPT[@]}" -gt 0 ]; then
      host="$(printf '%s' "${PIP_INDEX_OPT[1]}" | sed -n 's#^[a-z]*://\([^/]*\).*#\1#p')"
    fi
    [ -z "$host" ] && host="${PIP_TRUSTED_HOST:-}"
    if [ -n "$host" ]; then
      log "系统 CA 仍失败，降级为仅信任当前源 host：$host（关闭该 host 校验，仅此环境）"
      : >"$err"
      if PIP_PIP_INSECURE=1 "$pip" install --quiet --disable-pip-version-check "${PIP_INDEX_OPT[@]}" --trusted-host "$host" "$@" 2>"$err"; then
        rm -f "$err"; return 0
      fi
    fi
  fi
  cat "$err" >&2
  rm -f "$err"
  return 1
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
    if pip_install "$PS_DEPS_ROOT/toolchain/python/venv/bin/pip" -r "$req"; then
      log "pip 依赖已安装：$req"
    else
      log "pip 依赖安装失败：$req"
      return 1
    fi
  else
    log "缺 $req（deps-version.json → toolchains.python.configuration），跳过 pip"
  fi
}

install_qt() {
  if find_qt >/dev/null; then log "Qt $V_QT 已存在：$(find_qt)"; return 0; fi
  mkdir -p "$PS_DEPS_ROOT/toolchain/qt"

  local aqt="$PS_DEPS_ROOT/toolchain/python/venv/bin/aqt"
  if [ ! -x "$aqt" ]; then
    if pip_install "$PS_DEPS_ROOT/toolchain/python/venv/bin/pip" aqtinstall; then
      log "aqtinstall 已装入 venv"
    else
      aqt="$(command -v aqt || true)"
    fi
  fi
  if [ ! -x "$aqt" ]; then
    log "缺 aqt：先跑 --install --python（venv 里装 aqtinstall）"
    return 1
  fi

  # 轮询 deps-version.json → toolchains.qt.mirrors，选第一个可达的下载站传给 aqt --base
  local base base_opt
  base="$(pick_url qt /online/qtsdkrepository/linux_x64/desktop/)"
  base_opt=()
  [ -n "$base" ] && base_opt=(--base "$base")
  log "下载 Qt $V_QT 到 $PS_DEPS_ROOT/toolchain/qt（约 200MB；镜像=${base:-aqt默认}）"
  "$aqt" install-qt linux desktop "$V_QT" "$QT_ABI" \
    -O "$PS_DEPS_ROOT/toolchain/qt" --archives qtbase qtsvg qttools icu \
    "${base_opt[@]}" || {
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
  if [ -n "$V_RUST_CHANNEL" ] && ! ver_satisfy "$V_RUST_CHANNEL" "$V_RUST_VER"; then
    log "警告：$RUST_TOML 的 channel($V_RUST_CHANNEL) 低于 deps-version.json 门槛($V_RUST_MIN)"
  fi
  mkdir -p "$PS_DEPS_ROOT/toolchain/rust"
  export RUSTUP_HOME="$PS_DEPS_ROOT/toolchain/rust/rustup" CARGO_HOME="$PS_DEPS_ROOT/toolchain/rust/cargo"
  if [ ! -x "$CARGO_HOME/bin/cargo" ]; then
    # 轮询 deps-version.json → toolchains.rust.mirrors：逐个试 installer，第一个可达的用其
    # 配套 dist_server/update_root，避免国内 static.rust-lang.org 卡死。
    local picked installer dist_server update_root
    picked="$(pick_rustup)"
    IFS=$'\t' read -r installer dist_server update_root <<<"$picked"
    [ -z "$installer" ] && installer="https://sh.rustup.rs"
    log "安装 rustup + $V_RUST_REQ 到 $PS_DEPS_ROOT/toolchain/rust（installer=$installer）"
    if [ -n "$dist_server" ]; then export RUSTUP_DIST_SERVER="$dist_server"; fi
    if [ -n "$update_root" ]; then export RUSTUP_UPDATE_ROOT="$update_root"; fi
    curl -fsSL --connect-timeout 10 --retry 2 --retry-delay 1 "$installer" | \
      sh -s -- -y --no-modify-path --default-toolchain "$V_RUST_REQ" || return 1
    # 安装期镜像变量只作用于本次进程，不写进 shell 块：工具链已落地，后续 cargo 无需 dist 源。
  fi
  "$CARGO_HOME/bin/cargo" --version
}

# ---------- 主流程 ----------
case "$MODE" in
  --warm-cache)
    warm_cache
    exit $?
    ;;
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
    write_ccache_conf
    fails=0
    [ -n "$V_RUST_CHANNEL" ] && ! ver_satisfy "$V_RUST_CHANNEL" "$V_RUST_VER" && log "警告：$RUST_TOML channel($V_RUST_CHANNEL) 不满足 deps-version.json 要求($V_RUST_VER)"
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
    if [ "$DO_WARM" = 1 ]; then
      warm_cache
    else
      mb="$(ccache_cache_mb)"
      if ! cache_is_warm; then
        log "提示：ccache 缓存当前 $(ccache_cache_mb)MB，首次全量约几分钟，可执行："
        log "      tools/ubuntu-setup.sh --warm-cache   # 用 preset 分别预热 Release+Debug"
      fi
    fi
    log "完成。新终端生效；当前终端可： source ~/.bashrc"
    exit 0
    ;;
esac
