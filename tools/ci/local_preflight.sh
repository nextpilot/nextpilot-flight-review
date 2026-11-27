#!/usr/bin/env bash
# 推送前门禁：本地复现 CI 的「改动行增量」判定，基准统一为 origin/main。
#
# 用法：
#   tools/ci/local_preflight.sh               # quick（默认）：格式 / shell / workflow / mypy / 重排告警
#   tools/ci/local_preflight.sh --full        # 追加 构建 + ctest + tidy 增量 + arch_guard --quick
#   tools/ci/local_preflight.sh --base <ref>  # 换基准，默认 origin/main
#
# 口径：
#   - 只查本次改动（<base>...HEAD），与 CI 的 changed-lines 判定一致；
#   - 工具缺失打印 [skip] 并继续，不假装通过；退出码只由真实失败决定；
#   - 重排告警可用 ALLOW_REFORMAT=1 放行（仅限 owner 批准的独立 style 提交）。
#
# 权威说明见 docs/develop/push-gate.md。

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT" || exit 1

MODE=quick
BASE=origin/main
while [ $# -gt 0 ]; do
    case "$1" in
        --quick) MODE=quick; shift ;;
        --full) MODE=full; shift ;;
        --base) shift; BASE="${1:-origin/main}"; shift ;;
        -h|--help) sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "未知参数：$1（见 --help）"; exit 2 ;;
    esac
done

FAIL=0
SKIPPED=0
bold() { printf '\n\033[1m== %s ==\033[0m\n' "$1"; }
ok() { printf '  [ok] %s\n' "$1"; }
skip() { printf '  [skip] %s\n' "$1"; SKIPPED=$((SKIPPED + 1)); }
bad() { printf '  [FAIL] %s\n' "$1"; FAIL=$((FAIL + 1)); }
have() { command -v "$1" >/dev/null 2>&1; }
changed() { git diff --name-only --diff-filter=ACMR "$BASE"...HEAD -- "$@" 2>/dev/null; }

if ! git rev-parse --verify --quiet "$BASE" >/dev/null; then
    echo "基准 $BASE 不存在；先 git fetch origin（退化为 HEAD~1）"
    BASE=HEAD~1
fi
BASE_SHA="$(git rev-parse --short "$BASE")"

bold "0) 基准"
echo "  base = $BASE ($BASE_SHA)，head = $(git rev-parse --short HEAD)"
if git merge-base --is-ancestor "$BASE" HEAD; then
    ok "已包含基准"
else
    bad "未 rebase 到 $BASE：先 git fetch origin && git rebase $BASE"
fi

bold "1) 格式（C++ / Markdown / Python 的改动行）"
if [ ! -x tools/format.sh ]; then
    skip "tools/format.sh 不存在"
else
    CPP_FILES="$(changed '*.cpp' '*.h' '*.hpp' '*.cc')"
    MD_FILES="$(changed '*.md')"
    PY_FILES="$(changed '*.py')"
    if [ -n "$CPP_FILES" ]; then
        if have clang-format || have git-clang-format; then
            tools/format.sh --check "$BASE" --only cpp && ok "C++ 改动行格式" || bad "C++ 改动行格式（跑 tools/format.sh --staged 修）"
        else
            skip "clang-format 缺失，C++ 格式未查"
        fi
    fi
    if [ -n "$MD_FILES" ]; then
        tools/format.sh --check "$BASE" --only md && ok "Markdown 改动行格式" || bad "Markdown 改动行格式"
        if have npx; then
            npx --yes markdownlint-cli2@0.19.1 $MD_FILES >/dev/null 2>&1 && ok "markdownlint" || bad "markdownlint（看具体文件行）"
        else
            skip "node/npx 缺失，markdownlint 未查"
        fi
    fi
    if [ -n "$PY_FILES" ]; then
        tools/format.sh --check "$BASE" --only py && ok "Python 改动行格式" || bad "Python 改动行格式"
        if have python3 && python3 -c 'import mypy' >/dev/null 2>&1; then
            python3 -m mypy --config-file mypy.ini $PY_FILES && ok "mypy" || bad "mypy（无存量豁免）"
        else
            skip "mypy 未安装，Python 类型未查"
        fi
    fi
    if [ -z "$CPP_FILES$MD_FILES$PY_FILES" ]; then
        ok "无 C++/Markdown/Python 改动"
    fi
fi

bold "2) 脚本与 workflow"
SH_FILES="$(changed '*.sh')"
if [ -n "$SH_FILES" ]; then
    for f in $SH_FILES; do bash -n "$f" || bad "bash -n $f"; done
    ok "bash -n"
    if have shellcheck; then
        shellcheck -S error $SH_FILES && ok "shellcheck (error)" || bad "shellcheck (error)"
    else
        skip "shellcheck 未安装"
    fi
else
    ok "无 shell 改动"
fi
WF_FILES="$(changed '.github/workflows/*')"
if [ -n "$WF_FILES" ]; then
    if have actionlint; then
        actionlint .github/workflows/*.yml && ok "actionlint" || bad "actionlint"
    else
        skip "actionlint 未安装"
    fi
    python3 tools/check-workflow-outputs.py .github/workflows && ok "workflow 输出契约" || bad "workflow 输出契约"
else
    ok "无 workflow 改动"
fi

bold "3) 整文件重排告警"
if [ "${ALLOW_REFORMAT:-0}" = "1" ]; then
    skip "ALLOW_REFORMAT=1，跳过"
else
    REORDER=0
    for f in $(changed '*.cpp' '*.h' '*.hpp'); do
        A="$(git diff --numstat "$BASE"...HEAD -- "$f" | awk '{print $1+0}')"
        W="$(git diff -w --numstat "$BASE"...HEAD -- "$f" | awk '{print $1+0}')"
        if [ "${A:-0}" -gt 200 ] && [ "${A:-0}" -gt $((3 * (${W:-0} + 1))) ]; then
            echo "  [FAIL] $f：改动行 $A vs 忽略空白 $W —— 疑似整文件重排"
            REORDER=$((REORDER + 1))
        fi
    done
    if [ "$REORDER" -eq 0 ]; then ok "无整文件重排"; else bad "重排 $REORDER 个文件：拆出独立 style 提交或回退格式改动"; fi
fi

if [ "$MODE" = "full" ]; then
    bold "4) 构建 + ctest"
    . "${PS_DEPS_ENV:-/home/user/.ps-deps/env.sh}" 2>/dev/null || true
    export RUSTUP_TOOLCHAIN="${RUSTUP_TOOLCHAIN:-stable}" CARGO_NET_OFFLINE="${CARGO_NET_OFFLINE:-true}"
    if cmake -S . -B build-preflight -G Ninja -DCMAKE_BUILD_TYPE=Release -DPS_BUILD_TESTS=ON >/tmp/preflight-cmake.log 2>&1; then
        ok "configure"
        if cmake --build build-preflight -j"$(nproc)" >/tmp/preflight-build.log 2>&1; then
            ok "build"
            ctest --test-dir build-preflight -j"$(nproc)" --output-on-failure >/tmp/preflight-ctest.log 2>&1 \
                && ok "ctest 全绿" || bad "ctest 红（看 /tmp/preflight-ctest.log）"
        else
            bad "build 失败（看 /tmp/preflight-build.log）"
        fi
    else
        bad "configure 失败（看 /tmp/preflight-cmake.log）"
    fi

    bold "5) clang-tidy 增量（只断新增行）"
    CPP_LIST="$(changed '*.cpp')"
    if [ -z "$CPP_LIST" ]; then
        ok "无 .cpp 改动，tidy 跳过"
    elif ! have clang-tidy; then
        skip "clang-tidy 未安装"
    else
        if cmake -S . -B build-tidy -G Ninja -DPS_BUILD_TESTS=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/tmp/tidy-cmake.log 2>&1; then
            : >/tmp/tidy.log
            for f in $CPP_LIST; do
                echo "=== $f ===" >>/tmp/tidy.log
                clang-tidy -p build-tidy "$f" >>/tmp/tidy.log 2>&1
            done
            python3 tools/clang-tidy-diff-gate.py --log /tmp/tidy.log --base "$BASE" \
                && ok "tidy 新增行 0" || bad "tidy 新增行有告警（看 /tmp/tidy.log）"
        else
            bad "tidy configure 失败（看 /tmp/tidy-cmake.log）"
        fi
    fi

    bold "6) 架构反证"
    if [ -x tools/ci/arch_guard.sh ]; then
        tools/ci/arch_guard.sh --quick >/tmp/preflight-arch.log 2>&1 && ok "arch_guard --quick" || bad "arch_guard 红（看 /tmp/preflight-arch.log）"
    else
        skip "arch_guard.sh 缺失"
    fi
fi

printf '\n== 汇总：FAIL=%d SKIP=%d（mode=%s）==\n' "$FAIL" "$SKIPPED" "$MODE"
[ "$FAIL" -eq 0 ] || { echo "有失败项：修完再推（CI 的同一口径会红）"; exit 1; }
echo "通过：可以推送"
