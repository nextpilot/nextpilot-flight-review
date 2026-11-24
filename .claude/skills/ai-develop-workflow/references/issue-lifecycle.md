# issue 状态、交接与关闭

## 状态机

| 状态 | 负责人 | 含义 | 进入下一状态的条件 |
|---|---|---|---|
| `ready` | PM | 需求已定义，可领取 | Dev 领取 |
| `in-progress` | Dev | 正在实现 | 提交 PR |
| `in-review` | Dev → PM | 等待评审/验收 | PM 通过或退回 |
| `blocked` | PM | 有外部阻塞或待决策 | 阻塞解除 |
| `closed` | PM | PR 已合并，验收完成 | 关闭 issue |

状态写回 issue 正文顶部或评论，格式建议：

```text
> 状态：in-review
> 负责人：Dev-Agent-01
> 分支：fix/script-panel-overlap
> PR：!123
```

状态变更必须由对应角色回写；口头说明或聊天记录不算。

## 交接模板

每次交接都要让对方**只看 issue/PR 就能继续**，不能依赖上一轮对话记忆。

**Dev 领取：**

```text
claim: <agent-id>
branch: <branch-name>
plan: <实现步骤概述>
```

**Dev 完成：**

```text
done: <commit sha>
changed: <实际修改文件/模块>
tests: <命令 + 通过数量>
pr: <PR 号/链接>
deviations: <与 issue 的偏差；无则写 none>
```

**PM 验收：**

```text
accept: <验收结论>
criteria: <逐条验收标准是否满足>
follow-up: <需要后续处理的事项；无则写 none>
```

**阻塞：**

```text
blocked: <原因>
need: <需要 PM/用户决策的问题>
evidence: <日志、测试输出、失败命令>
next: <解除阻塞后的下一步>
```

## 交接纪律

- 接手的 Agent 必须先读 issue 最新正文、全部评论和 PR 最新 review/CI 状态，再开始动作。
- 同一 issue 同时只能有一个 Dev 在实现，避免并行改同一分支。
- PM 不改代码，Dev 不改验收标准；需要变更时由对应角色回写并说明原因。
- 任何“已完成”“已修复”“已验证”的结论，必须附可复现的命令或结果，不接受口头结论。

## 关闭 issue

全部满足才可关闭：

1. PR 状态为 `merged`，不是 `closed`。
2. 目标分支 CI/测试通过。
3. issue 的验收标准逐条满足。
4. issue 中已有完成记录评论：PR 号、commit、变更摘要、测试数字、偏差说明。
5. 需要同步的文档、配置、迁移说明已完成。

关闭动作由 PM 负责，Dev 不关闭 issue。平台不自动关闭（建 PR 时传 `close_related_issue: 0`），PM 在 PR 合并后显式执行：

- 目标 issue 仍 open：手动 `PATCH /repos/nextpilot/issues/<num>`（带 `repo` + `state=closed`）。
- 目标 issue 已 `closed`：补验收结论评论即可，无需重复关闭。
- 描述里引用过的其他 issue 一律不动；若有被误关的，恢复为 `state=open`。

收尾时确认远端分支是否已自动删除（取决于仓库设置），仍在则手动删；本地分支始终手动删。

## 重开 issue

出现以下情况必须重开：

- PR 已合并，但验收标准未真正满足。
- 合并后出现回归、测试失败或用户验收不通过。
- PR 被关闭但未合并，工作仍需继续。
- issue 被误关闭，仍有未完成项。
- 完成记录与实际变更不一致。

重开时 PM 必须写清：

```text
reopen: <原因>
evidence: <失败现象/测试/提交>
expected: <重新验收的标准>
```

重开后由 Dev 从最新 `main` 新建分支继续，**不复用已合并分支**。

## 另建后续 issue

以下情况不重开原 issue，另建后续 issue：

- 原 issue 已完成，但发现的是独立的新问题。
- 原范围之外的新需求。
- 需要单独排期、单独验收的后续优化。

新 issue 必须链接原 issue，注明“后续项”，避免历史丢失。
