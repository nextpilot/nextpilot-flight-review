---
name: ai-develop-workflow
description: 本仓库双角色 AI 开发工作流（产品经理 Agent + 开发者 Agent）。产品经理负责需求、issue、验收与关闭；开发者负责实现、验证、分支与 PR。标准任务走 issue → 分支 → PR → 合并 → 显式关闭 issue；简单低风险修改可免 issue，但仍必须走分支、PR 和验证。含修改前确认、issue 规范、issue 配图图床、状态机、多 Agent 交接、分支提交、提交信息格式（Conventional Commits + `Refs` 追溯）、PR 描述随提交同步（多提交全列）、Gitee 平台操作、合并后闭环校验与 PR 门槛。触发词：开发任务、修 bug、新建功能、任务计划、Plan、派发、回填、issue、PR、分支、github flow、commit、push、提交、推送、合代码、产品经理、开发者、验收、修改前确认、复述、更新 PR 描述、提交信息格式、commit 规范、截图、图床、配图。
---

# 开发工作流（PM/Dev 双角色 + GitHub Flow）

main 是唯一长期分支，始终可发布、可回退。所有改动经短命特性分支 + PR 进入 main，不设 develop 等中间分支。

本工作流模拟多个 Agent 协同开发。**issue 是需求与验收的载体，PR 是代码与验证的载体。** 两个角色、两条流程，不允许互相越权。

## 0. 前置纪律与必读索引

`references/` 下的文件不会自动全文注入。到对应步骤时，必须 `Read` 对应文件后再执行。

| 场景                                          | 必读                                |
| --------------------------------------------- | ----------------------------------- |
| 收到任何修改类指令，动手前                    | `references/change-confirmation.md` |
| 写代码注释、文档、提交信息、PR 描述、回复文字 | `.claude/rules/terse-writing.md`    |
| PM 建 issue / 更新 issue 正文                 | `references/issue-authoring.md`     |
| issue 里要放截图、给 issue 配图               | `references/issue-image-hosting.md` |
| Dev 领取、交接、PM 验收、关闭或重开 issue     | `references/issue-lifecycle.md`     |
| 建分支、提交、rebase、推送、建 PR、合并       | `references/git-pr-delivery.md`     |
| 调用 Gitee API、排查平台错误                  | `references/gitee-operations.md`    |

**修改前确认的硬要求：** 先复述“改什么、怎么改、哪里含糊”，等用户明确同意后才动手。沉默、未回复、话题转移、仅提问都不算同意。

**写作的硬要求：** 不引用出处，不追述历史；注释只写做什么、怎么做、必要的简单原因。完整规则以 `.claude/rules/terse-writing.md` 为准。

## 1. 核心原则

- **PM 决定“做什么、算不算完成”，Dev 负责“怎么改、有没有验证”。**
- 标准任务：**一个 issue = 一个特性分支 = 一个 PR**，顺序不可颠倒。
- 简单修改：允许免 issue，但**不允许免分支、免 PR、免验证**。
- 所有跨 Agent 的信息必须写回 issue 或 PR，不能只存在于对话上下文里。
- 一个 PR 只承载一个语义单元；相互独立的改动拆成多个 PR。
- 拿不准是否“简单”时，一律按标准流程建 issue。

## 2. 角色与职责

同一 Agent 可以在不同任务中扮演不同角色，但同一任务内不得混用。**每次接手任务时，先声明当前角色。**

### 2.1 产品经理 Agent（PM）

- 负责需求澄清、拆解和优先级判断。
- 负责创建、更新、重开和关闭 issue。
- 负责定义目标、范围、排除项和验收标准。
- 负责跟踪 issue 进度，处理阻塞、范围变更和验收争议。
- 负责审查 PR 是否满足 issue 的验收标准。
- PR 合并后，负责回填验收结论，并显式关闭目标 issue（平台不自动关闭）。
- 不直接修改业务代码；不替开发者决定实现细节。
- 不关闭未合并 PR 对应的 issue；不把“已提交 PR”当成“已完成”。

### 2.2 开发者 Agent（Dev）

- 负责领取 issue，按 issue 正文实现。
- 负责从最新 `main` 切出短命特性分支。
- 负责编码、编译、测试和自检。
- 负责提交 commit、推送分支、创建 PR。
- 负责在 PR 中关联 issue，写清变更内容和验证结果。
- 负责响应 review 和 CI 失败，在同一分支继续修复。
- 不擅自修改需求、验收标准或扩大范围；发现问题回报 PM。
- 不直接 push `main`，不自行关闭 issue，不自行合并 PR。

### 2.3 用户

- 最终验收人。
- 对高风险操作、强推、历史改写、绕过保护分支拥有最终授权。
- 可代替 PM 执行合并；合并后仍由 PM 关闭 issue。

## 3. 流程 A：标准 issue 流程

适用于新功能、缺陷修复、重构、契约/数据/构建/架构变更，以及任何拿不准是否“简单”的修改。

```text
PM 建 issue
  → Dev 领取并标记 in-progress
  → Dev 从最新 main 建分支
  → Dev 开发、测试、提交
  → Dev 推送并创建 PR，关联 issue
  → Dev 标记 in-review，请求 PM 验收
  → PM 审查：通过则合并；不通过则退回 Dev
  → PR merged 后，PM 回填验收结论、显式关闭目标 issue、自动删源分支
  → PM 校验闭环是否真的完成，缺失项手动兜底；Dev 删本地分支
```

| 步骤       | 负责人  | 动作                                                           | 完成标志                               | 必读                                        |
| ---------- | ------- | -------------------------------------------------------------- | -------------------------------------- | ------------------------------------------- |
| ① 建 issue | PM      | 查重后写 issue                                                 | 拿到 issue 编号（如 `IKX6N2`）         | `issue-authoring.md`                        |
| ② 领取     | Dev     | 在 issue 评论领取，声明分支和计划                              | issue 状态为 `in-progress`             | `issue-lifecycle.md`                        |
| ③ 建分支   | Dev     | 从最新 `main` 切出 `<类型>/<简述>`                             | 当前分支为特性分支                     | `git-pr-delivery.md`                        |
| ④ 开发验证 | Dev     | 编码、编译、跑测试，全部通过                                   | 构建无 error，测试全绿                 | —                                           |
| ⑤ 提交 PR  | Dev     | rebase 最新 main 后推送并建 PR                                 | PR 创建成功，关联 issue                | `git-pr-delivery.md`、`gitee-operations.md` |
| ⑥ 评审验收 | PM      | 按验收标准审查；不通过退回                                     | `in-review` → 通过或退回               | `issue-lifecycle.md`                        |
| ⑦ 合并     | PM/用户 | 合并 PR，回填验收结论评论                                      | PR `merged`，issue 有验收评论          | `issue-lifecycle.md`、`gitee-operations.md` |
| ⑧ 收尾校验 | PM/Dev  | 关闭目标 issue、校验源分支已删；缺失项手动兜底，Dev 删本地分支 | issue `closed`，远端与本地分支均不存在 | `gitee-operations.md`                       |

**关键约束：**

- PM 建 issue 前先查重。同一问题已有 open issue 的，更新原 issue，不重复创建。
- Dev 领取 issue 后才开工。同一 issue 同时只允许一个 Dev 负责。
- 分支必须从最新 `main` 切出；提交信息按 Conventional Commits 写（`type(scope): 摘要` + footer `Refs:`），简单流程写 `Refs: none`，破坏性变更加 `!` 与 `BREAKING CHANGE:`（见 `references/git-pr-delivery.md`）。
- PR 描述首行写 `Closes #<issue-id>`。建 PR 时必须显式传 `close_related_issue: 0`，平台便不在合并时自动关闭任何 issue，关闭动作由 PM 在合并后显式执行；PR 描述里的 issue 编号可照常引用其他 issue 用于说明来源与关系。按仓库设置自动删除源分支；PM 合并后必须校验实际状态，issue 未关或分支仍在时才手动处理。
- PR 描述随提交演进：分支每新增一个提交，都要按最新状态刷新描述，多提交必须逐条列出（见 `references/git-pr-delivery.md`）。
- PR 合并由 PM 或用户执行。Dev 只负责提交 PR 和响应评审。
- issue 只能由 PM 关闭。Dev 完成工作时标记 `in-review`，不能自行关闭。

**PR 不通过时：**

- 实现缺陷、测试失败、CI 失败：Dev 在同一分支追加提交修复，不另开 PR。
- 验收标准不清或需要变更：PM 更新 issue 正文，Dev 按新标准继续。
- 范围明显扩大：PM 拆出新 issue，原 issue 只保留原范围；新工作走新分支、新 PR。
- 无法继续：PM 标记 `blocked`，写明阻塞原因、需要谁决策、下一步动作。

## 4. 流程 B：简单修改，免 issue

适用于明确、低风险、可一次验证完成的修改。免 issue 不等于免流程。

### 4.1 适用条件

同时满足以下条件才算简单修改：

- 不新增功能，只修复明确缺陷，或修改文档、注释、格式、文案、单点小重构。
- 不改变公共 API、ABI、数据结构、持久化格式、构建依赖或架构边界。
- 不涉及权限、安全、数据迁移、兼容性策略。
- 改动集中在一个模块内，可在一个 PR 内完成并跑完回归。
- 不需要 PM 做需求取舍。

**拿不准时一律建 issue。** 不允许开发者先写代码，再倒推判断“这算简单修改”。

### 4.2 流程

```text
Dev 自检符合简单修改条件
  → 从最新 main 建分支
  → 开发、测试、提交
  → 创建 PR，声明 No-Issue
  → PM/用户评审并合并
  → 删除分支，结束
```

### 4.3 PR 要求

- 标题使用常规 `<type>: <动宾短语>`。
- 描述中必须写明：

```text
No-Issue: <为什么不需要 issue，例如 docs-only / 单文件文案修正 / 无行为变化>
Change-Type: docs | fix | chore | refactor
Verification: <编译与测试结果>
```

- 不写 `Closes`，不关联不存在的 issue。
- 分支追加提交后立即刷新描述：`Verification` 换成最新一次的结果，并补提交清单（见 `references/git-pr-delivery.md`）。
- 一旦发现改动触及契约、数据、依赖或范围扩大，立即停止免 issue 流程：由 PM 补建 issue，PR 改为关联该 issue，或关闭后重走标准流程。

## 5. 完成判定

标准 issue 流程同时满足才算完成：

- issue 状态为 `closed`（合并后由 PM 显式关闭）；
- 关联 PR 为 `merged`；
- 验收标准全部通过；
- 完成记录已回填；
- 提交信息符合 Conventional Commits 且含 `Refs:`；
- PR 描述与实际提交一致（多提交逐条列出）；
- 特性分支已删除（远端与本地）；
- 文档/配置同步已完成；
- **PR 建时传了 `close_related_issue: 0`**，合并后描述里引用过的其他 issue 仍为 `open`（详见 `references/gitee-operations.md`）。

简单修改流程同时满足才算完成：

- PR 为 `merged`；
- 编译与测试通过；
- PR 中 `No-Issue` 理由成立；
- 提交信息符合 Conventional Commits 且含 `Refs: none`；
- PR 描述与实际提交一致（多提交逐条列出）；
- 分支已删除；
- **PR 建时传了 `close_related_issue: 0`**；描述里若引用了其他 issue 编号，仅作说明，不构成关闭目标。

## 6. AI 执行纪律

- AI 不直接 push `main`；标准任务走「issue → 特性分支 → PR → 合并 → 关 issue」全流程。
- 简单修改可按 §4 免 issue，但必须走「特性分支 → PR → 验证 → 合并」。
- 用户显式说「直接推 main / 强推」时可例外，但本项目 `main` 为保护分支，技术上大概率被拒，应提前说明。
- PR 验证失败：修复后追加提交再推，不 amend 已推送的提交；推送后同步刷新 PR 描述。
- 合并后先校验闭环（目标 issue 是否已关、源分支是否已删），不以推断代替校验；缺什么补什么。
- 迁移期例外：结构性大改（目录重组、历史清理）经用户拍板可单节点强推 `main` 或用 contents API 清理；迁移定稿后本例外作废，一律按本闭环。
- 全程不把令牌、截图、调试产物写进仓库。issue 配图上传到图床仓库，不进业务仓（见 `references/issue-image-hosting.md`）。
- 危险操作（force push、历史改写、绕过保护分支）先按 `references/change-confirmation.md` 复述并取得用户确认。

## 7. Reference 索引

| 文件                                | 内容                                             |
| ----------------------------------- | ------------------------------------------------ |
| `references/change-confirmation.md` | 修改前复述、等待同意、执行流程、自查             |
| `references/issue-authoring.md`     | issue 载体、9 章节、模板、质量标准               |
| `references/issue-image-hosting.md` | issue 配图：图床仓库、目录约定、上传与引用、排错 |
| `references/issue-lifecycle.md`     | 状态机、交接模板、关闭与重开                     |
| `references/git-pr-delivery.md`     | 分支、提交、rebase、推送、PR、合并门槛           |
| `references/gitee-operations.md`    | 令牌、API 端点、平台踩坑、合并后闭环校验         |

写作规范不在 `references/` 下，唯一正文是 `.claude/rules/terse-writing.md`。
