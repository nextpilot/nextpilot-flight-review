#!/usr/bin/env bash
# 编译加速环境：把 ccache 装到持久目录 $HOME/.ps-deps/，再把 PATH / CCACHE_* 写进
# shell 启动文件，使 CMakeLists 里的 find_program(ccache) 无需 -DCMAKE_PROGRAM_PATH 也能命中。
#
#   tools/setup-build-env.sh              # 安装 / 更新（幂等，可重复执行）
#   tools/setup-build-env.sh --check      # 只报告状态，不改任何文件
#   tools/setup-build-env.sh --uninstall  # 从启动文件里移除配置（保留 ccache 本体）
#
# 为什么不在 CMakeLists 里写 HINTS：路径一旦写死就绑死单机，换机器 / 换 HOME 就失效。
# 环境差异留在 shell 层，仓库只保留可移植的探测逻辑。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="${PS_DEPS_DIR:-$HOME/.ps-deps}"
CCACHE_HOME="$DEPS/ccache"
CCACHE_CACHE="$DEPS/ccache-cache"
MARK_BEGIN="# >>> plotscope build env >>>"
MARK_END="# <<< plotscope build env <<<"
STARTUP_FILES=("${BASHRC:-$HOME/.bashrc}" "${PROFILE:-$HOME/.profile}")
MODE="${1:---install}"

log() { printf '[build-env] %s\n' "$*"; }

have_path_ccache() { command -v ccache >/dev/null 2>&1; }

# 把系统 ccache 做成自包含目录：环境重置常清 /usr/bin 下的东西，
# 连带 apt 拉进来的 libzstd / libhiredis 也会消失，所以依赖库一起拷走。
make_selfcontained() {
  local src="$1"
  rm -rf "$CCACHE_HOME"
  mkdir -p "$CCACHE_HOME/bin" "$CCACHE_HOME/libexec" "$CCACHE_HOME/lib"
  cp "$(command -v "$src")" "$CCACHE_HOME/libexec/ccache-real"
  local l base
  while IFS= read -r l; do
    base="$(basename "$l")"
    case "$base" in
      libc.so.* | libm.so.* | ld-linux*) continue ;;
    esac
    [ -f "$l" ] && cp -n "$l" "$CCACHE_HOME/lib/" 2>/dev/null || true
  done < <(ldd "$(command -v "$src")" | awk '/=> \//{print $3}')
  cat >"$CCACHE_HOME/bin/ccache" <<'WRAPPER'
#!/bin/bash
# 自包含入口：补 LD_LIBRARY_PATH，并把 argv[0] 伪装成 ccache。
# 直接以别的名字启动会被 ccache 当成交叉编译前缀，报 "Could not find compiler"。
SELF_DIR=$(cd "$(dirname "$0")" && pwd)
export LD_LIBRARY_PATH="$SELF_DIR/../lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec -a ccache "$SELF_DIR/../libexec/ccache-real" "$@"
WRAPPER
  chmod +x "$CCACHE_HOME/bin/ccache" "$CCACHE_HOME/libexec/ccache-real"
  log "已就绪：$CCACHE_HOME/bin/ccache"
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

write_block() {
  local f="$1"
  touch "$f"
  strip_block "$f"
  cat >>"$f" <<EOF
$MARK_BEGIN
export PATH="$CCACHE_HOME/bin:\$PATH"
export CCACHE_DIR="$CCACHE_CACHE"
export CCACHE_MAXSIZE=5G
export CCACHE_BASEDIR="$ROOT"
$MARK_END
EOF
  log "已写入：$f"
}

show_status() {
  log "仓库根：$ROOT"
  if [ -x "$CCACHE_HOME/bin/ccache" ]; then
    log "ccache：$CCACHE_HOME/bin/ccache（$("$CCACHE_HOME/bin/ccache" --version | head -1)）"
  elif have_path_ccache; then
    log "ccache：PATH 中 $(command -v ccache)（尚未拷到持久目录）"
  else
    log "ccache：未安装"
  fi
  local f
  for f in "${STARTUP_FILES[@]}"; do
    if [ -f "$f" ] && grep -qF "$MARK_BEGIN" "$f"; then
      log "启动文件：$f 已配置"
    else
      log "启动文件：$f 未配置"
    fi
  done
  if command -v ccache >/dev/null 2>&1; then
    log "当前 shell 可见 ccache：$(command -v ccache)"
  else
    log "当前 shell 看不到 ccache —— 新开终端，或 source 一下启动文件"
  fi
}

case "$MODE" in
  --check)
    show_status
    ;;
  --uninstall)
    for f in "${STARTUP_FILES[@]}"; do
      strip_block "$f" && log "已清理：$f"
    done
    log "ccache 本体保留在 $CCACHE_HOME，需要彻底删除请手动 rm -rf"
    ;;
  --install | "")
    if [ ! -x "$CCACHE_HOME/bin/ccache" ]; then
      if ! have_path_ccache; then
        log "PATH 里没有 ccache，尝试用 apt 安装（需要 sudo）"
        if command -v apt-get >/dev/null 2>&1; then
          sudo apt-get update -qq && sudo apt-get install -y ccache || true
        fi
      fi
      if have_path_ccache; then
        make_selfcontained ccache
      else
        log "无法获取 ccache：请先安装（apt install ccache），或把二进制放到 $CCACHE_HOME/bin/ccache 后重跑"
        exit 1
      fi
    else
      log "复用已存在的 $CCACHE_HOME/bin/ccache"
    fi
    mkdir -p "$CCACHE_CACHE"
    for f in "${STARTUP_FILES[@]}"; do
      write_block "$f"
    done
    log "完成。新开终端后 cmake configure 会打印 [ccache] 已启用；本终端立即生效请执行："
    log "  export PATH=\"$CCACHE_HOME/bin:\$PATH\" && export CCACHE_DIR=\"$CCACHE_CACHE\""
    ;;
  *)
    log "未知参数：$MODE（可用 --check / --uninstall）"
    exit 2
    ;;
esac
