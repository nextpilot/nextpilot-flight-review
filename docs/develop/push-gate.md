# 推送前门禁

推之前把 CI 会拦的项在本地过一遍，用 [`tools/ci/local_preflight.sh`](../../tools/ci/local_preflight.sh) 一条命令。

## 基准

改动行增量判定（`format` / `markdown` / `python` / `python-types` / `tidy`）在本地和 CI 都用 `origin/main` 当基准：

```bash
git fetch origin
git rebase origin/main
```

CI 与本地同一口径：特性分支和手动触发取 `merge-base(origin/main, HEAD)`，合入 `main` 的推送取 `event.before`（不可达退 `HEAD~1`）。解析集中在 `tools/ci/resolve-base.sh`，`format` / `markdown` / `python` / `tidy` 共用。本地不 rebase，两边基准不一致，仍然会出现"本地过、推上去红"。

## 命令

```bash
tools/ci/local_preflight.sh           # quick：格式 / 脚本 / workflow / mypy / 重排告警
tools/ci/local_preflight.sh --full    # 改过 C++，或推分支前最后一遍：加 构建 + ctest + tidy + arch_guard
```

参数：`--base <ref>` 换基准（默认 `origin/main`）；`ALLOW_REFORMAT=1` 放行重排告警，仅限 owner 批准的独立 `style:` 提交。

工具缺失打印 `[skip]` 并继续，退出码只由真实失败决定。**`[skip]` 落在你改过的类型上，要自己确认对应 CI job 不会红**。

`--full` 的构建落在 `build-preflight-Release` 与 `build-preflight-Debug` 两个目录：Debug 不清 `NDEBUG`，`assert` 与析构顺序这类崩溃只在它现形。tidy 用 `build-tidy`。互不干扰，也不碰现有 `build/`。

## 三档与 CI job

| 档            | 何时跑                     | 检查项                                                                                                                       | CI job                                                               |
| ------------- | -------------------------- | ---------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------- |
| quick（默认） | 每次推之前                 | 基准对齐、C++/Markdown/Python 改动行格式、markdownlint、bash -n、shellcheck、actionlint、workflow 输出契约、mypy、整文件重排 | `lint:format` `markdown` `python` `shell` `workflows` `python-types` |
| full          | 改过 `.cpp/.h`，或推分支前 | quick + Release/Debug 双构建 + 双 ctest + clang-tidy 增量 + `arch_guard --quick`                                             | `ci:test` `test-clang` `test-sanitizers` `config-matrix` `lint:tidy` |

`tidy` 只断新增行：`tools/clang-tidy-diff-gate.py` 取 diff 新增行号与 warning 行号的交集，存量约 329 条不阻断。命中存量行说明基准错了。

## 三条硬禁令

1. **不整文件重排。** 重排把改动行撑到上千行，tidy 拿历史 warning 撞红，blame 报废。统一风格单独一笔 `style:` 提交，事先说清。
2. **不放宽门禁换绿。** 不改断言、不删测试、不加 `#ifdef` 绕守卫、不把判据从 0 改成 ≤N、不改 `.clang-tidy` 白名单。
3. **不用 `--no-verify`、不 force-push `main`、不在旧 base 上推。** `SKIP_PREFLIGHT=1` 只用于失败项与本 PR 无关的紧急推送，并在 PR 写明原因。

## 本地验不了的三项

| 项                | 典型表现                                                               | 处理                            |
| ----------------- | ---------------------------------------------------------------------- | ------------------------------- |
| `macos-compile`   | termios 缺 `B921600` / `B460800`、bash 3.2 变量名、AppleClang 告警差异 | owner 指派修，修完在 PR 重跑    |
| `windows-compile` | MSVC 行为差异、sccache 命中异常                                        | 同上                            |
| GUI 起窗          | `ps_test_gui` 存量约 1/30 SegFault                                     | 先确认是否存量；新增的当 bug 修 |

这三项红了不许改 CI 放宽。

## 红了怎么查

1. 报错行号与本地对不上：先 `rebase origin/main`，再看。
2. `format` / `markdown`：`tools/format.sh --staged` 修完重提交。
3. `tidy`：看命中的是不是你新增的行；命中存量行是基准错。
4. 平台腿：对照上表，走 owner 指派。
5. 真 bug：修。

## 合并前

CI 全绿不等于可合。主线合并前同时满足：

1. 本地 `--full` 通过，含 `main` 与本分支合并后的状态；
2. PR 最后一轮 CI 全绿；
3. 四步门禁（APPROVE / testers / test APPROVE / merge）走完；
4. owner 点头。

## hook

```bash
tools/ubuntu-setup.sh --install        # 环境 + 仓库存在 .githooks 时注册 hook
tools/ubuntu-setup.sh --install-hooks  # 只注册 hook；--uninstall-hooks / --check-hooks 对应卸载/检查
```

- `pre-commit`：格式化暂存区（只修不拦），并对疑似整文件重排告警。
- `pre-push`：跑 `local_preflight.sh` quick，失败阻断。
- 非 Git 工作树（ubuntu-setup 被单独下载运行）跳过 hook 注册与检查；在 Git 工作树内 `.githooks` 缺失时 `--check` 直接失败。
- CMake configure 会自动注册 hook 并执行 `tools/ubuntu-setup.sh --check`；check 不过先自动 `--install` 修复后复查，复查仍不过才终止配置并给出手动命令；CI 环境跳过。
- 绕过：`SKIP_PREFLIGHT=1 git push`、`git commit --no-verify`。不建议，CI 仍会拦。
