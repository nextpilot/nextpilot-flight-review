#!/usr/bin/env bash
# 解析 CI 的改动行基准，输出一个可给 `git diff <base>...HEAD` 用的 ref。
#
# 用法（在 workflow 的 Resolve base 步骤里）：
#   tools/ci/resolve-base.sh "${{ github.event_name }}" "${{ github.base_ref }}" \
#       "${{ github.event.before }}" "${GITHUB_REF_NAME}"
#
# 规则：
#   pull_request        → origin/<base_ref>
#   workflow_dispatch   → merge-base(origin/main, HEAD)
#   推送到 main         → event.before（不可达时 HEAD~1）
#   推送到特性分支      → merge-base(origin/main, HEAD)
#
# 特性分支用 merge-base 而不是 event.before：后者只覆盖"本次推了几个提交"，
# 分支从 main 切出后 main 又前进了，两边基准就不同，本地过、CI 红。
set -euo pipefail

EVENT="${1:?event_name}"
BASE_REF="${2:-}"
BEFORE="${3:-}"
REF_NAME="${4:-}"

fetch_main() {
    git fetch -q --no-tags --depth=50 origin \
        +refs/heads/main:refs/remotes/origin/main 2>/dev/null || true
}

case "$EVENT" in
    pull_request)
        echo "origin/${BASE_REF}"
        ;;
    workflow_dispatch)
        fetch_main
        git merge-base origin/main HEAD
        ;;
    *)
        if [ "$REF_NAME" = "main" ]; then
            if [ -n "$BEFORE" ] && git cat-file -e "${BEFORE}^{commit}" 2>/dev/null; then
                echo "$BEFORE"
            else
                git rev-parse HEAD~1
            fi
        else
            fetch_main
            if git rev-parse --verify --quiet origin/main >/dev/null \
                && git merge-base origin/main HEAD; then
                :
            elif [ -n "$BEFORE" ] && git cat-file -e "${BEFORE}^{commit}" 2>/dev/null; then
                echo "$BEFORE"
            else
                git rev-parse HEAD~1
            fi
        fi
        ;;
esac
