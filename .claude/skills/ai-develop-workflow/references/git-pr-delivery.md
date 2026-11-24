# Git、分支与 PR 交付

## 分支规则

| 规则 | 内容 |
|---|---|
| 命名 | `<类型>/<简述>`：`feat/`、`fix/`、`docs/`、`refactor/`、`chore/`（如 `fix/script-panel-overlap`）；功能用 `feat/`、缺陷用 `fix/` |
| 寿命 | 短命：一个分支只承载一个工作单元，合并后即删 |
| 起点 | 一律从**最新** `main` 切出；开工前先 fetch + 对齐 `origin/main` |
| 禁止 | 直接向 `main` push（保护分支会拒绝）；复用已合并分支；一个分支夹带多个不相关改动 |
| 对应 | 标准流程：分支名/提交信息/PR 描述都要能反查到 issue 编号。简单流程：PR 必须声明 `No-Issue` 理由 |

## 提交纪律

- 三阶段：**开发期**（编码与本地验证，不受分支限制）、**提交期**（git commit）、**推送期**（git push）受纪律约束。
- 一个提交一个完整语义单元（编译通过、不破坏测试）；禁止“临时存一版”的废提交。
- 禁止在主干分支上先 `git commit` 再迁移；应先检出特性分支，后提交。
- 作者身份按项目约定设置（本项目 `latercomer <latercomer@qq.com>`），用 `git -c user.name=... -c user.email=... commit` 逐次指定，不改全局配置。

### 提交信息格式（Conventional Commits）

采用 [Conventional Commits 1.0.0](https://www.conventionalcommits.org/zh-hans/v1.0.0/)，footer 承载追溯字段：

```text
<type>(<scope>)!: <摘要>

<正文：为什么改 / 改了什么 / 影响面>

Refs: <issue-id>
PR: #<n>
BREAKING CHANGE: <影响与迁移方式>
```

| 部分 | 规则 |
|---|---|
| `type` | 必填，与分支前缀一致：`feat` 新增功能 / `fix` 缺陷修复 / `docs` 文档 / `style` 仅格式 / `refactor` 重构 / `perf` 性能 / `test` 测试 / `build` 构建依赖 / `ci` 流水线 / `chore` 杂务 / `revert` 回滚。分支前缀不在表内（如 `legacy/`）取最贴近的 type |
| `scope` | 可选，模块名（`gui`、`registry`、`workflow`）；跨模块或全局改动省略括号 |
| `!` | 破坏性变更标记：`feat(api)!: 移除 v1 接口`，同时必须在 footer 写 `BREAKING CHANGE:` |
| 摘要 | ≤72 字符，动宾短语，不带句号；禁止 `update`/`fix` 这类无信息量表述 |
| 正文 | 与标题空一行；每行 ≤72 字符；写为什么、改了什么、影响面 |
| footer | 与正文空一行，每行一条 `Token: value`。`Refs:` **必填**（标准流程填 issue 编号 `IKX79J`，简单流程填 `Refs: none`）；`PR:` 可选；`BREAKING CHANGE:` 按需 |

- type 表达改动性质而非分支归属：`fix/` 分支上顺手补的文档用 `docs`，不硬写成 `fix`。
- `Refs:` 开工前就已确定，所以能强制；`PR:` 提交时不可预知，**禁止为补它而 amend 已推送提交或强推**。
- PR ↔ 提交的完整映射由 PR 描述的「提交清单」承担，不靠 commit 单点承载；issue ↔ PR 由 `Closes #IKxxxxx` 关联，形成三方可回溯。
- 本规范只约束之后的提交；历史提交不回改（改写历史禁止）。

推送前格式自查：

```bash
# ① 标题行：type 在白名单内，摘要非空
git log origin/main..HEAD --format='%h %s' \
  | grep -vE '[0-9a-f]{7,} (feat|fix|docs|style|refactor|perf|test|build|ci|chore|revert)(\([a-z0-9._-]+\))?!?: .' \
  || echo "标题行全部合规"

# ② 追溯字段：每个提交必须有 Refs
for c in $(git rev-list origin/main..HEAD); do
  git log -1 --format=%B "$c" | grep -q '^Refs: ' \
    || echo "缺 Refs: $(git log -1 --format='%h %s' "$c")"
done

# ③ 破坏性变更：标题带 ! 的必须有 BREAKING CHANGE footer
for c in $(git rev-list origin/main..HEAD); do
  if git log -1 --format=%s "$c" | grep -qE '^[a-z]+(\([^)]+\))?!: '; then
    git log -1 --format=%B "$c" | grep -q '^BREAKING CHANGE: ' \
      || echo "缺 BREAKING CHANGE: $(git log -1 --format='%h %s' "$c")"
  fi
done
```

## 判定与检出

```bash
git branch --show-current
```

- 当前为特性分支 → 转“提交与本地测试”。
- 当前为主干分支且有 feature 改动 → 从最新 `main` 检出特性分支：

```bash
git fetch origin
git checkout main && git reset --hard origin/main
git checkout -b <类型>/<简述>
```

未提交改动随检出自动携带，无需 stash；主干上未推送的本地 feature 提交随分支一并携带，主干回退至远端原点。

- HEAD 游离或仓库无提交 → 先与用户确认再操作。

## 提交与本地测试

1. `git add` 相关文件后提交（遵循“提交纪律”，作者用 `git -c` 指定）。
2. 推送前测试必须通过：编译、ctest/项目测试全绿；失败先修复；无测试条件须在提交说明注明。
3. 相互独立的改动拆成多个提交，禁止合并为单一提交。

## rebase 后推送

```bash
git fetch origin
git rebase origin/main
git push origin <特性分支>
```

- rebase 冲突逐个解决后 `git rebase --continue`；无法解决则 `git rebase --abort` 回滚并报告。禁止为绕过冲突改用 merge 提交或强推。
- 推送被拒（远端有新提交）→ 重新执行本步骤；禁止 `--force`。
- `--force`、`--no-verify` 仅在用户明确要求时使用；hook 失败表明真实问题，应修根因。
- 推送前自检：待推 ref 非主干；`git log origin/main..HEAD` 仅含本次 feature 提交；测试全绿；已 rebase 至远端最新；提交信息通过“提交信息格式”的两项自查。
- PR 已存在时，推送成功后立即按新提交刷新 PR 描述（见“PR 描述随提交同步更新”）。

## PR 规则

1. 推送特性分支后建 PR：`base: main ← compare: 特性分支`。建 PR 必须显式传 `close_related_issue: 0`，平台便不在合并时自动关闭任何 issue；建 PR 后回读 PR 详情确认该字段为 `0`。
2. **标准流程 PR** 描述必含：
   - 首行 `Closes #<issue-id>`；
   - issue 链接；
   - 做什么、为什么；
   - 验证方式与结果（测试数字）；
   - 提交清单（多提交逐条列出，见下节）；
   - 与 issue 的偏差（无则写 none）。
   - 描述里可正常引用其他 issue 编号用于说明来源与关系，不因连带关闭风险而删减。
3. **简单流程 PR** 描述必含：
   - `No-Issue: <理由>`；
   - `Change-Type: ...`；
   - 验证方式与结果；
   - 提交清单（多提交逐条列出）。
4. 合并门槛（全部满足才能合）：
   - 全量编译无 error；ctest 全绿（口径见 `docs/develop/testing.md`）；
   - 无越层依赖（分层守卫随 configure 强制）；
   - 涉及架构/契约/文档的改动已同步对应文档；
   - PR 内每个提交符合 Conventional Commits 且 footer 含 `Refs:`（自查见“提交信息格式”）；
   - PR 描述与实际提交一致（多提交逐条列出）；
   - Gitee 侧 review 已通过、CI 门禁已过（操作见 `references/gitee-operations.md`）。
5. 合并方式：普通 merge 或 squash（保持 `main` 历史线性、每节点语义完整）。
6. 合并由 PM 或用户执行；Dev 不自行合并。
7. 合并后按 `references/gitee-operations.md` 关闭目标 issue、校验源分支是否已删，缺失项才手动兜底；本地分支始终手动删。

## PR 描述随提交同步更新

PR 描述不是一次性产物。分支上每新增一个提交（review 修复、补测、追加改动），都要立即刷新描述，保持「描述 = 分支实际内容」。

```bash
# 先读现有描述，在原文上增量更新，不要凭空重写
curl -s "https://gitee.com/api/v5/repos/nextpilot/nextpilot-advance-plotscope/pulls/<n>?access_token=<TOKEN>"

# 再整段替换
curl -s -X PATCH "https://gitee.com/api/v5/repos/nextpilot/nextpilot-advance-plotscope/pulls/<n>" \
  -H "Content-Type: application/json" \
  -d '{"access_token":"<TOKEN>","body":"<更新后的完整描述>"}'
```

- **多提交必须全列**：描述末尾「提交清单」逐条写 `<短哈希> <type>: <摘要>`，与 `git log origin/main..HEAD` 一致。禁止只写首个提交，禁止沿用它只有一个提交时的旧描述。
- **摘要与测试结果按最新状态重写**：测试数字取最新一次全量回归；已修好的问题从待处理中移除，不把过期结论留在描述里。
- **PATCH 是整段替换**：先 `GET` 再改，保留 review 讨论中引用过的段落结构和他人补充。
- **纪律**：新提交推送后未更新描述即视为 PR 未就绪；描述与分支提交不一致的 PR 不合并。
