#!/usr/bin/env bash
# 统一格式化 / 校验入口：C/C++、Markdown、Python 三类。
#
# 口径（三类一致）：
#   首次已全量，之后只处理改动文件；CI 对改动文件做硬校验，本地 hook 只做修复。
#
# 各后端的细节差别：
#   C/C++   —— 仓库不做整树重排（原因见 .clang-format 头注释），只格式变更行。
#              --all 默认跳过 C++，必须显式 --cpp 才整树重排（一次性动作，慎用）。
#   Markdown —— prettier，版本与 CI 严格一致（版本漂移会让本地"已格式化"在 CI 仍红）。
#   Python  —— ruff（format + check 一个工具），版本取 tools/requirements.txt。
#
# 用法：
#   tools/format.sh --staged              修复暂存区改动（pre-commit 调用）
#   tools/format.sh --check [BASE]        校验改动相对 BASE（CI 调用，默认 HEAD~1）
#   tools/format.sh --all                 全量格式化 md + py
#   tools/format.sh --all --cpp           全量格式化 md + py + C/C++
#
# 可选：
#   --only md|py|cpp    只处理某一类（CI 按类型拆 job 时用）
#   --cpp               允许 --all 触及整树 C/C++（默认禁止）

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 1

PRETTIER_VERSION_DEFAULT=3.10.1
PRETTIER_VERSION="${PRETTIER_VERSION:-$PRETTIER_VERSION_DEFAULT}"

MODE=""
BASE=""
ONLY="all"
ALLOW_CPP_ALL=0

usage() {
  sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
  case "$1" in
    --all) MODE=all ;;
    --staged) MODE=staged ;;
    --check)
      MODE=check
      shift
      # 可选的 BASE：只有不以 -- 开头的下一个参数才当 base。
      if [ $# -gt 0 ] && [ "${1#--}" = "$1" ]; then
        BASE="$1"
        shift
      fi
      continue
      ;;
    --only)
      shift
      ONLY="${1:-all}"
      ;;
    --only=*) ONLY="${1#--only=}" ;;
    --cpp) ALLOW_CPP_ALL=1 ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "format: 未知参数 '$1'" >&2
      exit 2
      ;;
  esac
  shift
done

if [ -z "$MODE" ]; then
  usage
  exit 2
fi

# ---------------------------------------------------------------- 工具解析

PRETTIER=()
if command -v prettier >/dev/null 2>&1; then
  got="$(prettier --version 2>/dev/null || true)"
  [ "$got" = "$PRETTIER_VERSION" ] ||
    echo "format: 警告：本地 prettier 是 ${got:-未知}，仓库钉的是 $PRETTIER_VERSION" >&2
  PRETTIER=(prettier)
elif command -v npx >/dev/null 2>&1; then
  PRETTIER=(npx --yes "prettier@$PRETTIER_VERSION")
fi

RUFF="$(command -v ruff || true)"
[ -z "$RUFF" ] && [ -x "$HOME/.local/bin/ruff" ] && RUFF="$HOME/.local/bin/ruff"

CF="$(command -v "${CLANG_FORMAT:-clang-format}" 2>/dev/null || true)"
GCF="$(command -v "${GIT_CLANG_FORMAT:-git-clang-format}" 2>/dev/null || true)"

# staged 模式（hook）工具缺失只提示、不阻断提交；check 模式（CI）缺失即失败。
missing() {
  local tool="$1" hint="$2"
  if [ "$MODE" = "staged" ]; then
    echo "format: 未找到 $tool，跳过该类型（$hint；CI 仍会检查）" >&2
    return 0
  fi
  echo "format: 未找到 $tool，无法校验（$hint）" >&2
  return 1
}

want() { [ "$ONLY" = "all" ] || [ "$ONLY" = "$1" ]; }

# ---------------------------------------------------------------- 文件收集

MARKDOWN_FILES=()
PYTHON_FILES=()

collect_md() {
  MARKDOWN_FILES=()
  local f
  while IFS= read -r -d '' f; do
    case "$f" in
      src/3rdparty/*) continue ;;
      *.md) MARKDOWN_FILES+=("$f") ;;
    esac
  done
}

collect_py() {
  PYTHON_FILES=()
  local f
  while IFS= read -r -d '' f; do
    case "$f" in
      src/3rdparty/*) continue ;;
      *.py) PYTHON_FILES+=("$f") ;;
    esac
  done
}

# ---------------------------------------------------------------- 各后端

md_write() {
  [ "${#MARKDOWN_FILES[@]}" -gt 0 ] || return 0
  echo "format: markdown ${#MARKDOWN_FILES[@]} 个文件"
  "${PRETTIER[@]}" --write -- "${MARKDOWN_FILES[@]}"
}

md_check() {
  [ "${#MARKDOWN_FILES[@]}" -gt 0 ] || return 0
  echo "format: markdown ${#MARKDOWN_FILES[@]} 个文件"
  "${PRETTIER[@]}" --check -- "${MARKDOWN_FILES[@]}"
}

py_write() {
  [ "${#PYTHON_FILES[@]}" -gt 0 ] || return 0
  echo "format: python ${#PYTHON_FILES[@]} 个文件"
  "$RUFF" format --quiet -- "${PYTHON_FILES[@]}" && "$RUFF" check --fix --quiet -- "${PYTHON_FILES[@]}"
}

py_check() {
  [ "${#PYTHON_FILES[@]}" -gt 0 ] || return 0
  echo "format: python ${#PYTHON_FILES[@]} 个文件"
  "$RUFF" format --check -- "${PYTHON_FILES[@]}"
}

cpp_check() {
  local base="$1"
  local files
  files=$(git -c core.quotePath=false diff --name-only --diff-filter=d "$base"...HEAD \
    -- '*.cpp' '*.cc' '*.cxx' '*.h' '*.hpp' | grep -v '^src/3rdparty/' || true)
  [ -z "$files" ] && { echo "format: 本次改动没有 C/C++ 变更"; return 0; }
  echo "format: C/C++ 变更行（相对 $base）"
  # shellcheck disable=SC2086  # 逐文件列表按空白切分是有意的：路径不含空格。
  "$GCF" --binary "$CF" --diff "$base" -- $files
}

cpp_write_staged() {
  local files
  files=$(git diff --cached --name-only --diff-filter=ACM \
    | grep -E '\.(cpp|cc|cxx|h|hpp)$' || true)
  [ -z "$files" ] && return 0
  # git clang-format 就地修改工作区，返回码 1 表示"有改动"，不是失败。
  # 不带文件列表：--staged 本身就只看暂存区，额外传路径在不同版本上行为不一致。
  "$GCF" --binary "$CF" --staged >/dev/null 2>&1 || true
  # shellcheck disable=SC2086
  git add -- $files
}

# ---------------------------------------------------------------- 主流程

RC=0

case "$MODE" in
  staged)
    if want cpp; then
      if [ -z "$CF" ] || [ -z "$GCF" ]; then
        missing "clang-format / git-clang-format" "见 docs/develop/building.md" || RC=1
      else
        cpp_write_staged || RC=1
      fi
    fi

    if want md; then
      if [ "${#PRETTIER[@]}" -eq 0 ]; then
        missing "prettier（需要 Node/npx）" "npm i -g prettier@$PRETTIER_VERSION" || RC=1
      else
        collect_md < <(git -c core.quotePath=false diff --cached --name-only -z --diff-filter=ACM)
        if md_write; then
          [ "${#MARKDOWN_FILES[@]}" -eq 0 ] || git add -- "${MARKDOWN_FILES[@]}"
        else
          RC=1
        fi
      fi
    fi

    if want py; then
      if [ -z "$RUFF" ]; then
        missing "ruff" "pip install 'ruff==0.14.6'" || RC=1
      else
        collect_py < <(git -c core.quotePath=false diff --cached --name-only -z --diff-filter=ACM)
        if py_write; then
          [ "${#PYTHON_FILES[@]}" -eq 0 ] || git add -- "${PYTHON_FILES[@]}"
        else
          RC=1
        fi
      fi
    fi
    ;;

  check)
    if [ -z "$BASE" ] || ! git rev-parse --verify --quiet "${BASE}^{commit}" >/dev/null 2>&1; then
      BASE="HEAD~1"
    fi
    echo "format: base = $BASE"

    if want cpp; then
      if [ -z "$CF" ] || [ -z "$GCF" ]; then
        missing "clang-format / git-clang-format" "见 docs/develop/building.md" || RC=1
      else
        cpp_check "$BASE" || RC=1
      fi
    fi

    if want md; then
      if [ "${#PRETTIER[@]}" -eq 0 ]; then
        missing "prettier（需要 Node/npx）" "npm i -g prettier@$PRETTIER_VERSION" || RC=1
      else
        collect_md < <(git -c core.quotePath=false diff --name-only -z --diff-filter=d "${BASE}...HEAD" -- '*.md')
        md_check || RC=1
      fi
    fi

    if want py; then
      if [ -z "$RUFF" ]; then
        missing "ruff" "pip install 'ruff==0.14.6'" || RC=1
      else
        collect_py < <(git -c core.quotePath=false diff --name-only -z --diff-filter=d "${BASE}...HEAD" -- '*.py')
        py_check || RC=1
        [ "${#PYTHON_FILES[@]}" -gt 0 ] && { "$RUFF" check --quiet -- "${PYTHON_FILES[@]}" || RC=1; }
      fi
    fi
    ;;

  all)
    if want md; then
      if [ "${#PRETTIER[@]}" -eq 0 ]; then
        missing "prettier（需要 Node/npx）" "npm i -g prettier@$PRETTIER_VERSION" || RC=1
      else
        collect_md < <(git -c core.quotePath=false ls-files -z '*.md')
        md_write || RC=1
      fi
    fi

    if want py; then
      if [ -z "$RUFF" ]; then
        missing "ruff" "pip install 'ruff==0.14.6'" || RC=1
      else
        collect_py < <(git -c core.quotePath=false ls-files -z '*.py')
        py_write || RC=1
      fi
    fi

    if want cpp; then
      if [ "$ALLOW_CPP_ALL" -eq 0 ]; then
        echo "format: 跳过 C/C++ 整树格式化（仓库不做 bulk reformat；确需执行请加 --cpp）"
      elif [ -z "$CF" ]; then
        missing "clang-format" "见 docs/develop/building.md" || RC=1
      else
        tree_files=$(git -c core.quotePath=false ls-files '*.cpp' '*.cc' '*.cxx' '*.h' '*.hpp' \
          | grep -v '^src/3rdparty/' || true)
        # shellcheck disable=SC2086
        "$CF" -i $tree_files
        echo "format: C/C++ 整树已重排，提交后请登记到 .git-blame-ignore-revs"
      fi
    fi
    ;;
esac

exit "$RC"
