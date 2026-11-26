#!/usr/bin/env python3
"""校验 workflow 里 `steps.<id>.outputs.<key>` 的输出契约是否成立。

GitHub Actions 不校验这件事：id 写错、key 拼错，都不会报错，运行时只拿到空串。
空串继续往下传，往往变成「命令参数缺失」——比如 base 变空后就是
`git diff ""...HEAD`（非法 ref），而失败点离真正原因隔着几十行。

更隐蔽的是加 `if:` 的门：条件不满足时整段步骤被跳过，通道坏了也显示绿。
所以这里做一次静态对齐：**引用了哪个 step 的哪个 output，那个 step 就必须真的写出过它。**

用法：
    tools/check-workflow-outputs.py .github/workflows          # 目录
    tools/check-workflow-outputs.py .github/workflows/ci.yml   # 单文件

退出码：0 = 全部一致；1 = 存在未声明的引用；2 = 参数/文件问题。
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

# job 头：2 空格缩进的 `name:`（jobs: 的直接子键）
JOB_RE = re.compile(r"^  ([A-Za-z0-9_-]+):\s*$")
# step 头：6 空格缩进的 `- `
STEP_RE = re.compile(r"^      - ")
STEP_ID_RE = re.compile(r"^\s*id:\s*([A-Za-z0-9_-]+)")
# 写 outputs 的两种写法：echo "key=..." / printf 'key=%s\n' ...
OUT_WRITE_RE = re.compile(r"""^\s*(?:echo|printf)\s+["']?([A-Za-z_][A-Za-z0-9_-]*)=""")
GITHUB_OUTPUT_RE = re.compile(r'>>\s*"?\$GITHUB_OUTPUT"?')
# 引用
REF_RE = re.compile(r"steps\.([A-Za-z0-9_-]+)\.outputs\.([A-Za-z0-9_-]+)")


def check_file(path: Path) -> list[str]:
    """返回该文件里所有契约不成立的引用描述。"""
    lines = path.read_text(encoding="utf-8").splitlines()
    problems: list[str] = []

    job: str | None = None
    step_id: str | None = None
    # (job, step_id) -> 已写出的 output keys
    writes: dict[tuple[str, str], set[str]] = {}
    # (job, step_id) -> 定义所在行号（用于区分「id 不存在」与「key 没写」）
    defined: dict[tuple[str, str], int] = {}
    # job -> {step_id: 首次定义行号}
    job_steps: dict[str, dict[str, int]] = {}
    refs: list[tuple[str | None, int, str, str]] = []

    for no, line in enumerate(lines, start=1):
        m = JOB_RE.match(line)
        if m:
            job = m.group(1)
            step_id = None
            job_steps.setdefault(job, {})
            continue

        if STEP_RE.match(line):
            step_id = None
            continue

        m = STEP_ID_RE.match(line)
        if m and job is not None:
            step_id = m.group(1)
            defined[(job, step_id)] = no
            job_steps[job].setdefault(step_id, no)
            writes.setdefault((job, step_id), set())
            continue

        # 同一 step 内的 output 写入（job 为 None 的行不在任何 job 内，前面已过滤掉 id 绑定）
        if job is not None and step_id is not None and GITHUB_OUTPUT_RE.search(line):
            m = OUT_WRITE_RE.match(line)
            if m:
                writes.setdefault((job, step_id), set()).add(m.group(1).rstrip("="))

        for m in REF_RE.finditer(line):
            refs.append((job, no, m.group(1), m.group(2)))

    for job, no, ref_id, ref_key in refs:
        if job is None:
            problems.append(f"{path}:{no}: 引用 steps.{ref_id}.outputs.{ref_key}，但不在任何 job 内")
            continue
        if ref_id not in job_steps.get(job, {}):
            problems.append(
                f"{path}:{no}: job `{job}` 里引用了 steps.{ref_id}.outputs.{ref_key}，但没有 id: {ref_id} 的 step"
            )
            continue
        if ref_key not in writes.get((job, ref_id), set()):
            have = sorted(writes.get((job, ref_id), set())) or ["（无）"]
            problems.append(
                f"{path}:{no}: job `{job}` 引用 steps.{ref_id}.outputs.{ref_key}，"
                f"但该 step（第 {defined[(job, ref_id)]} 行）只写出过 {', '.join(have)}"
            )
    return problems


def main() -> int:
    args = sys.argv[1:]
    if not args:
        args = [".github/workflows"]

    targets: list[Path] = []
    for a in args:
        p = Path(a)
        if p.is_dir():
            targets.extend(sorted(p.glob("*.yml")) + sorted(p.glob("*.yaml")))
        elif p.is_file():
            targets.append(p)
        else:
            print(f"[outputs] 找不到路径：{a}", file=sys.stderr)
            return 2

    if not targets:
        print("[outputs] 没有可检查的 workflow 文件", file=sys.stderr)
        return 2

    all_problems: list[str] = []
    for t in targets:
        all_problems.extend(check_file(t))

    if all_problems:
        print(f"[outputs] 发现 {len(all_problems)} 处输出契约不成立：")
        for msg in all_problems:
            print(f"  {msg}")
        print(
            '[outputs] 修法：要么给对应 step 补 `echo "key=value" >> "$GITHUB_OUTPUT"`，要么改引用点的 step id / key。'
        )
        return 1

    print(f"[outputs] {len(targets)} 个 workflow，steps.x.outputs.y 引用全部有对应声明")
    return 0


if __name__ == "__main__":
    sys.exit(main())
