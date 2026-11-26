#!/usr/bin/env python3
"""clang-tidy 增量门禁：只让「本次变更新增的行」上的 finding 阻断 CI。

为什么要按行而不是按条数：全仓存量有几百条 init-variables / use-override，
一刀切成硬门禁会让每个 PR 直接变红，然后所有人都开始忽略这个检查。
按行比对的好处是——存量不背锅，但只要你在某个文件里新写了一行带问题的代码，
它立刻红，而且报的就是你刚写的那一行。

用法（CI 里由 lint.yml 调用）：
    tools/clang-tidy-diff-gate.py --log tidy.log --base origin/main [--max 200]

退出码：0 = 新增行无 finding；1 = 新增行有 finding（已打印清单）；
        2 = 门禁没跑起来（基线缺失 / git diff 失败）——按**阻断**处理。
            宁可红一次让人来看，也不让门禁静默放过：检查通过必须是「真的比对过」，
            而不是「没比成所以没报错」。
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from collections import defaultdict

HUNK_RE = re.compile(r"^\+\+\+ b/(?P<file>.+)$")
RANGE_RE = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@")
WARN_RE = re.compile(r"^(?P<file>.+?):(?P<line>\d+):(?P<col>\d+): (?:warning|error): ")


def added_lines(base: str, root: str) -> dict[str, set[int]]:
    """返回 {相对路径: 新增行号集合}，只看 .cpp/.cxx（clang-tidy 的 TU 范围）。"""
    out = subprocess.run(
        ["git", "diff", "-U0", "--diff-filter=d", f"{base}...HEAD", "--", "*.cpp", "*.cxx"],
        cwd=root,
        capture_output=True,
        text=True,
    )
    if out.returncode != 0:
        print(f"[gate] git diff 失败：{out.stderr.strip()}", file=sys.stderr)
        sys.exit(2)

    result: dict[str, set[int]] = defaultdict(set)
    cur: str | None = None
    for line in out.stdout.splitlines():
        m = HUNK_RE.match(line)
        if m:
            cur = m.group("file")
            continue
        m = RANGE_RE.match(line)
        if m and cur:
            start = int(m.group(1))
            count = int(m.group(2)) if m.group(2) is not None else 1
            result[cur].update(range(start, start + count))
    return dict(result)


def findings(log_path: str, root: str) -> list[tuple[str, int, str]]:
    """解析 clang-tidy 输出，把绝对路径归一成仓库相对路径。"""
    items: list[tuple[str, int, str]] = []
    with open(log_path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            m = WARN_RE.match(line)
            if not m:
                continue
            path = m.group("file")
            if path.startswith(root):
                path = os.path.relpath(path, root)
            items.append((path, int(m.group("line")), line.strip()))
    return items


def resolve_base(root: str) -> str | None:
    """--base 缺失时的兜底：优先 origin/main，其次 HEAD~1；都不可用返回 None。

    这里只是防御编排侧漏传（比如 outputs 里少了 base=）。真正该给的基线仍由
    lint.yml 的 `Collect changed C++ sources` 计算，不能靠猜替代。
    """
    for ref in ("origin/main", "HEAD~1"):
        r = subprocess.run(
            ["git", "rev-parse", "--verify", "--quiet", f"{ref}^{{commit}}"],
            cwd=root,
            capture_output=True,
        )
        if r.returncode == 0:
            return ref
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--log", required=True, help="clang-tidy 原始输出")
    ap.add_argument("--base", required=True, help="diff 基线 ref")
    ap.add_argument("--root", default=".", help="仓库根（默认当前目录）")
    ap.add_argument("--max", type=int, default=50, help="最多打印多少条")
    args = ap.parse_args()

    root = os.path.abspath(args.root)
    base = (args.base or "").strip()
    if not base:
        base = resolve_base(root) or ""
        if not base:
            # 连兜底基线都找不到：门禁没有基线可算 → fail-closed 阻断。
            # 宁可红一次让人来看，也不静默放过（绿了但没检查比红更危险）。
            print(
                "[gate] --base 为空且无可用基线（origin/main / HEAD~1 都不可用）："
                "门禁没有基线可算 → 按阻断处理（fail-closed）",
                file=sys.stderr,
            )
            return 2
        print(f"[gate] --base 为空，回退到 {base}（检查 lint.yml 的 outputs.base 是否漏写）")
    touched = added_lines(base, root)
    if not touched:
        print("[gate] 本次没有新增的 C++ 行，跳过")
        return 0

    all_findings = findings(args.log, root)
    hits = []
    seen: set[tuple[str, int, str]] = set()
    for path, line, raw in all_findings:
        if line in touched.get(path, set()):
            key = (path, line, raw.split("warning: ")[-1])
            if key in seen:
                continue
            seen.add(key)
            hits.append(raw)

    total = len(all_findings)
    if not hits:
        print(f"[gate] 存量 finding {total} 条，新增行 0 条 → 通过")
        return 0

    print(f"[gate] 本次新增行上的 clang-tidy finding：{len(hits)} 条（存量 {total} 条不计）")
    for h in hits[: args.max]:
        print(f"  {h}")
    if len(hits) > args.max:
        print(f"  ... 其余 {len(hits) - args.max} 条见 artifact/tidy.log")
    return 1


if __name__ == "__main__":
    sys.exit(main())
