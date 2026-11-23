---
name: ai-develop-workflow
description: 本仓库统一开发工作流（任务计划 + GitHub Flow + 提交推送）。任何开发任务、派发/编写 Plan、回填完成情况、commit/push 都必须遵循：先建 issue → 从 remote main 拉最新并建特性分支 → 开发验证 → 回填 issue → 建 PR → 待 PR 合并后自动关闭 issue → 结束。含 Plan 内容规范、分支命名、提交纪律、Gitee API 实操、PR 门槛。触发词：开发任务、修 bug、新建功能、任务计划、Plan、派发、回填、issue、PR、分支、github flow、commit、push、提交、推送、合代码。
---

# 开发工作流（Plan + GitHub Flow + 提交推送）

main 是唯一长期分支、始终可发布可回退。所有改动以 **issue 为任务载体**，经短命特性分支 + PR 进入 main，不设 develop 等中间分支。

**一个任务 = 一个 issue = 一个特性分支 = 一个 PR。** 四者一一对应，缺一不可，顺序不可颠倒。

## 1. 端到端闭环（每个任务严格按此顺序）

```
①建 issue → ②同步 remote main → ③建特性分支 → ④开发+验证 → ⑤回填 issue → ⑥推送+建 PR → ⑦PR 合并后自动关 issue → 结束
```

| 步骤 | 动作 | 完成标志 |
|---|---|---|
| ① 建 issue | 收到任务**第一件事**就是建 issue，正文内容按 §2 写（目标/范围/验收标准） | 拿到 issue 编号（如 `IKX6N2`） |
| ② 同步 main | `git fetch origin` → `git checkout main` → `git reset --hard origin/main`（或 `pull --ff-only`） | 本地 main == origin/main |
| ③ 建特性分支 | 从**最新** main 切出 `<类型>/<简述>` 分支（规则见 §3） | `git branch --show-current` 为特性分支 |
| ④ 开发+验证 | 在特性分支编码、编译、跑测试，全部通过 | ctest/测试全绿、构建无 error |
| ⑤ 回填 issue | 在本 issue 评论写「完成情况」（提交号、变更、测试结果、偏差，见 §2.4） | issue 中有完成记录评论 |
| ⑥ 推送+建 PR | rebase 至最新 main 后推送特性分支（细节见 §4），建 PR（见 §6） | PR 创建成功（拿到 PR 号） |
| ⑦ 关 issue+收尾 | **轮询 PR 状态直到 merged**，随后在 issue 回填完成说明评论、自动关闭 issue（见 §5.3）、删除特性分支 | issue 有完成记录评论且 `state=closed`，分支已删 |

**关键约束：**

- **先有 issue 再动手**。禁止"先写代码后补 issue"；任务再小（含文档、配置）也先建 issue。
- **⑦ 必须自动关闭**：建完 PR 不是终点。要持续跟踪 PR 状态，一旦合并立即关闭 issue，形成闭环；禁止 PR 合并后把 issue 晾着 open。
- 任一步未达标不得进入下一步；验证失败在步骤 ④ 内修复后重跑，不携带失败状态前进。

## 2. 任务计划（Plan / issue）内容规范

Plan 是设计师与执行 agent 之间的**完整工作契约**：执行 agent 不依赖对话历史，仅凭 issue 正文即可正确开工并交付。

### 2.1 载体管理

- **唯一载体**：gitee issue；禁止在仓库写 plan md 文件（避免污染仓库）。
- **平台**：`nextpilot/nextpilot-advance-plotscope`；后续迁 GitHub 随仓库一起搬。
- **组织**：大阶段建一个**父 issue**（总 Plan + 任务清单）+ 同名里程碑；每个工作单元一个**子 issue**，独立派发、评论、关闭。
- **标题**：`[<阶段字母><步骤号><子步骤字母>] <动宾标题>`，如 `[P1a] 机制库 libs/archive：ZipReader 读端`（阶段字母自选但同主题一致；步骤号对应里程碑条目；子步骤字母可选）。
- **标签**：主题标签（如 `M3`、`toolbox`）+ 归属里程碑。
- **粒度**：一个 issue = 一个可独立验收的工作单元，禁止多个工作单元合并。
- **状态**：open = 待执行/进行中；评论回填后 closed = 完成，永久可查。
- **职责**：设计师定稿方案并建 issue、验收；执行 agent 按正文实施、遇缺陷中止报告（不得自行变更方案）；用户验收批准。

### 2.2 内容构成（6 问 → 9 章节，顺序固定不得增删合并）

| 问题 | 章节 | 要点 |
|---|---|---|
| 为什么做 | ① 任务标识 ② 背景与依据 | ①编号/动宾标题/状态行（状态/执行agent/验收人/里程碑条目）；②问题陈述、现状盘点（给代码路径、可复用机制禁止重造）、依赖声明（前置 issue 链接 + 设计文档章节锚点） |
| 做什么 | ③ 目标与范围 | 目标用**可观测结果**表述（不用"完善/优化"单独成条）；**排除项必须显式列出** |
| 怎么做 | ④ 设计方案 | 只记本单元新决策（接口草图/数据格式/理由）；与里程碑文档重复处用锚点引用，禁止复制全文；临时决定标注"本单元拍板" |
| 在哪做 | ⑤ 变更清单 | 精确到文件路径：新增/修改/禁止触碰（后者给原因），禁用"相关模块"等模糊表述 |
| 怎么做对 | ⑥ 实施步骤 ⑦ 验收标准 | ⑥按施工顺序编号、每步附验证方式、末步固定全量回归（写清配置×命令×通过标准）；⑦逐条可判定（测试名/行为断言/量化指标/产物），禁止"基本可用"等表述 |
| 怎么算做完 | ⑧ 完成记录 ⑨ 交付后义务 | ⑧由执行 agent **完成后回填**、禁止预填，只记事实；⑨完成即须同步的文档/代码清单，未执行不算完成 |

### 2.3 质量标准

1. **自包含**：不读对话历史即可开工；
2. **可判定**：验收标准每条可机械判定，步骤每步有验证方式；
3. **单一事实来源**：设计内容只在设计文档或 plan 一处完整存在，另一处用锚点引用；
4. **边界明确**：排除项与禁止触碰清单完备；
5. **最小必要**：不含无关内容，超范围需求记入后续 plan。

### 2.4 完成记录回填（步骤 ⑤）

仅记事实：提交号、实际变更文件（与⑤对照，超出须说明原因）、测试结果（数字）、与 plan 的偏差（做了什么 plan 未写的、什么未做、原因）。禁止"基本完成"一类不可判定结论。

### 2.5 标准模板

```markdown
# [<阶段字母><步骤号><子步骤字母>] <动宾标题>

> 状态：待执行｜执行 agent：（领取后填写）｜验收人：（用户）
> 关联：里程碑条目 <Mx.y>｜设计依据 <设计文档 §锚点>

## ① 任务标识
<编号、标题、状态行；一句话任务陈述>

## ② 背景与依据
- 问题陈述：<为何需要本单元>
- 现状盘点：<已有底座（代码路径）/ 缺口>
- 依赖：<前置 plan 链接 / 设计文档锚点>

## ③ 目标与范围
- 目标：<可观测结果>
- 排除项：<本单元明确不做>

## ④ 设计方案
<本单元新增决策：接口草图 / 数据格式 / 理由；重复内容给锚点>

## ⑤ 变更清单
- 新增：`path`（说明）
- 修改：`path`（变更内容）
- 禁止触碰：`path`（原因）

## ⑥ 实施步骤
1. <步骤>（验证：<判定方式>）
N. 全量回归（口径：<配置 × 命令 × 通过标准>）

## ⑦ 验收标准
- [ ] <测试名 / 行为断言 / 量化指标 / 产物>
- [ ] 回归：<口径>

## ⑧ 完成记录
（执行 agent 回填：提交号 / 实际变更 / 测试结果 / 偏差）

## ⑨ 交付后义务
- [ ] <文档/代码同步项>
```

## 3. 分支与提交纪律

### 3.1 分支规则

| 规则 | 内容 |
|---|---|
| 命名 | `<类型>/<简述>`：`feat/`、`fix/`、`docs/`、`refactor/`、`chore/`（如 `fix/script-panel-overlap`）；功能用 `feat/`、缺陷用 `fix/` |
| 寿命 | 短命：一个分支只承载一个工作单元（对应一个 issue），合并后即删 |
| 起点 | 一律从**最新** main 切出；开工前先 fetch + 对齐 origin/main |
| 禁止 | 直接向 main push（保护分支会拒绝，见 §5）；复用已合并分支；一个分支夹带多个不相关改动 |
| 对应 | 分支名/提交信息/PR 描述都要能反查到 issue 编号 |

### 3.2 提交纪律

- 三阶段：**开发期**（编码与本地验证，不受分支限制）、**提交期**（git commit）、**推送期**（git push）受纪律约束。
- 一个提交一个完整语义单元（编译通过、不破坏测试）；禁止"临时存一版"的废提交。
- 提交信息：`<type>: <动宾短语>`（type 同分支前缀），正文写清做了什么、为什么；禁止 `update`/`fix` 等无信息量表述。
- 提交信息引用 issue 编号（如 `IKX6N2`），便于自动关联。
- 作者身份按项目约定设置（本项目 `latercomer <latercomer@qq.com>`），用 `git -c user.name=... -c user.email=... commit` 逐次指定，不改全局配置。
- 禁止在主干分支上先 `git commit` 再迁移；应先检出特性分支，后提交。

## 4. git 提交与推送执行（步骤 ④→⑥）

### 4.1 判定与检出

```bash
git branch --show-current
```

- 当前为特性分支 → 转 §4.2。
- 当前为主干分支且有 feature 改动 → 从最新 main 检出特性分支：
  ```bash
  git fetch origin
  git checkout main && git reset --hard origin/main
  git checkout -b <类型>/<简述>
  ```
  未提交改动随检出自动携带，无需 stash；主干上未推送的本地 feature 提交随分支一并携带，主干回退至远端原点。
- HEAD 游离或仓库无提交 → 先与用户确认再操作。

### 4.2 提交与本地测试

1. `git add` 相关文件后提交（遵循 §3.2，作者用 `git -c` 指定）。
2. 推送前测试必须通过：编译、ctest/项目测试全绿；失败先修复；无测试条件须在提交说明注明。
3. 相互独立的改动拆成多个提交，禁止合并为单一提交。

### 4.3 rebase 后推送

```bash
git fetch origin
git rebase origin/main
git push origin <特性分支>
```

- rebase 冲突逐个解决后 `git rebase --continue`；无法解决则 `git rebase --abort` 回滚并报告。禁止为绕过冲突改用 merge 提交或强推。
- 推送被拒（远端有新提交）→ 重新执行本步骤；禁止 `--force`。
- `--force`、`--no-verify` 仅在用户明确要求时使用；hook 失败表明真实问题，应修根因。
- 推送前自检：待推 ref 非主干；`git log origin/main..HEAD` 仅含本次 feature 提交；测试全绿；已 rebase 至远端最新。

## 5. Gitee 平台实操（本项目专属，务必按此执行）

本仓库是**企业 `nextpilot` 下的仓库**，main 为**保护分支**，普通个人仓库的 API 路径大多不可用。

### 5.1 认证与令牌（三种方式，均已实测）

令牌是**仓库级私人令牌**，所属账号 `latercomer`。**令牌运行时从用户获取，严禁写入仓库任何文件**（包括本 skill）。

**① git 推送**——URL 内嵌令牌：

```bash
# 用户名必须是"令牌所属账号"，不是仓库属主！
git push https://latercomer:<TOKEN>@gitee.com/nextpilot/nextpilot-advance-plotscope.git <refspec>
```

⚠️ 写 `nextpilot:<TOKEN>` 会报 403 `The token username invalid`——即令牌有效，用户名对不上账号也不行（实测踩坑）。拿不准所属账号时先查：`GET /api/v5/user?access_token=<TOKEN>` 返回的 `login` 字段就是正确用户名。

**② API 调用**——推荐 **body/query 带 `access_token` 参数**（所有已验证端点均用此形式，最稳）：

```bash
# JSON 形式（建 PR 等）
curl -s -X POST "https://gitee.com/api/v5/repos/nextpilot/nextpilot-advance-plotscope/pulls" \
  -H "Content-Type: application/json" \
  -d '{"access_token": "<TOKEN>", "title": "...", ...}'

# 表单形式（建 issue/评论/关 issue 等，--data-urlencode 自动转义）
curl -s -X POST "https://gitee.com/api/v5/repos/nextpilot/issues" \
  --data-urlencode "access_token=<TOKEN>" \
  --data-urlencode "repo=nextpilot-advance-plotscope" \
  --data-urlencode "title=..."
```

**③ header 形式**（部分端点可用，不如 ② 可靠，不推荐首选）：

```bash
curl -s -H "Authorization: token <TOKEN>" "https://gitee.com/api/v5/repos/..."
```

所需 scope：`issues`（建/关 issue）、`notes`（评论）、`pull_requests`（PR）。企业级操作（`/enterprises/...` 路径）仓库级令牌无权，一律走 §5.2 的 repos 扁平路径。

### 5.2 关键 API 端点（均已验证）

| 操作 | 方法 + 路径 | 备注 |
|---|---|---|
| 建 issue | `POST /api/v5/repos/nextpilot/issues` | 表单参数：`repo=nextpilot-advance-plotscope` + `title`/`body` + `issue_type=任务`（企业扁平路径，**路径里不带仓库名**） |
| 读/列 issue | `GET /api/v5/repos/nextpilot/nextpilot-advance-plotscope/issues` | `number` 形如 `IKX6N2` |
| 追加评论（回填） | `POST /api/v5/repos/nextpilot/nextpilot-advance-plotscope/issues/<num>/comments` | 表单参数 `body`（Markdown） |
| 读评论 | `GET /api/v5/repos/nextpilot/nextpilot-advance-plotscope/issues/<num>/comments` | enterprises 路径会因仓库级令牌报 403，用 repos 路径 |
| 建 PR | `POST /api/v5/repos/nextpilot/nextpilot-advance-plotscope/pulls` | JSON body：`head`/`base`/`title`/`body` |
| 更新 PR 描述 | `PATCH /api/v5/repos/nextpilot/nextpilot-advance-plotscope/pulls/<n>` | JSON body：`body`（新描述全文）；关联 issue 用 `Closes IKxxxxx` 写进描述首行 |
| 审批 PR | `POST /api/v5/repos/nextpilot/nextpilot-advance-plotscope/pulls/<n>/review` | body 带 `event=APPROVE`；成功返回 204 |
| 查 PR 状态 | `GET /api/v5/repos/nextpilot/nextpilot-advance-plotscope/pulls/<n>` | 看 `state`（`open`/`merged`/`closed`）、`mergeable` |
| 合并 PR | `PUT /api/v5/repos/nextpilot/nextpilot-advance-plotscope/pulls/<n>/merge` | body 带 `merge_method=merge` |
| 关闭 issue | `PATCH /api/v5/repos/nextpilot/issues/<num>` | **必须** 表单参数带 `repo=nextpilot-advance-plotscope` + `state=closed`，返回 `state: closed` |
| 删文件（免强推） | `DELETE /api/v5/repos/nextpilot/nextpilot-advance-plotscope/contents/<path>` | body 带 `sha`/`branch`/`message`；清理误传文件用，绕过保护分支 |

**踩坑提示：**

- **git 推送用户名 = 令牌所属账号**：`https://nextpilot:<TOKEN>@...` → 403 `The token username invalid`（令牌本身有效也没用）；改 `https://latercomer:<TOKEN>@...` 即成功。不确定所属账号先 `GET /api/v5/user?access_token=<TOKEN>` 看 `login`。
- **项目型仓库 issue 只走扁平路径**：`POST/PATCH /repos/nextpilot/issues`（仓库名放表单参数 `repo`）。标准路径 `POST/PATCH /repos/nextpilot/nextpilot-advance-plotscope/issues/<num>` → 404/「project or enterprise」——项目型仓库的 issue 编号是 `IKxxxxx` 标识串，不是纯数字。
- 关闭 issue **只能**用 `PATCH /repos/nextpilot/issues/<num>` + `repo=`。`PATCH /enterprises/nextpilot/issues/<num>` → 403「仓库级私人令牌无权」。
- main 是保护分支：**任何直推**（哪怕带令牌）都返回 `No permission to push this protected branch`。改动只能经 PR；清理误传文件用 contents API（见上表），不强推。
- PR 合并可能被门禁拦截：`未通过设置的审查`（需先 approve）或 `未通过设置的测试`（CI 门禁）。遇此先排查 review 与 status，不绕过。
- **不要把调试/截图图片传进仓库**。评论框「上传图片」是图床（存服务器、不进仓库），但只认网页登录 cookie，令牌调用返回 401；v5 API 无开放图床上传端点。需要内嵌图片时用网页登录后上传或外链，禁止 `git add` 截图。

### 5.3 PR 合并后自动关闭 issue（步骤 ⑦ 的实现）

```
loop（每隔一段时间，设上限避免无限等待）：
    GET PR 状态
    if state == "merged":
        POST /repos/nextpilot/nextpilot-advance-plotscope/issues/<num>/comments
            # 先在 issue 上回填「完成情况」评论：PR 号、提交号、变更摘要、
            # 测试结果（数字）、与 plan 的偏差——关闭前 issue 里必须有完成记录
        PATCH /repos/nextpilot/issues/<issue编号>  { repo, state=closed }   # 关闭 issue
        git push origin --delete <特性分支>；git branch -D <特性分支>        # 删分支
        结束
    elif state == "closed":   # PR 被关但未合并
        报告用户，不关 issue，查明原因
    else:                     # open
        继续等待；若有 CI/review 未过则先修复
```

- 关闭前确认 PR 确实 `merged`，**不要根据"已提交 PR"就提前关闭**。
- 若 CLI 会话无法持续轮询，交付说明中明确写下「PR 号 + 待合并后关闭 issue IKXxxxx」待办，由下次会话接续，不能默认已完成。

## 6. PR 规则

1. 推送特性分支后建 PR：`base: main ← compare: 特性分支`。
2. PR 描述必含：做什么、关联 issue（`#<编号>` 或链接）、验证方式与结果（测试数字）。
3. 合并门槛（全部满足才能合）：
   - 全量编译无 error；ctest 全绿（口径见 `docs/develop/testing.md`）；
   - 无越层依赖（分层守卫随 configure 强制）；
   - 涉及架构/契约/文档的改动已同步对应文档；
   - Gitee 侧 review 已通过、CI 门禁已过（见 §5.2）。
4. 合并方式：普通 merge 或 squash（保持 main 历史线性、每节点语义完整）。
5. 合并后按 §5.3 立即关闭 issue 并删除特性分支（本地+远端）。

## 7. AI 执行纪律与完成判定

- AI 不直接 push main；即使是文档小改也走「建 issue → 特性分支 → PR → 关 issue」全流程。
- 用户显式说「直接推 main / 强推」时可例外，但本项目 main 为保护分支，技术上大概率被拒，应提前说明。
- PR 验证失败：修复后追加提交再推，不 amend 已推送的提交。
- 迁移期例外：结构性大改（目录重组、历史清理）经用户拍板可单节点强推 main 或用 contents API 清理；迁移定稿后本例外作废，一律本闭环。
- 全程不把令牌、截图、调试产物写进仓库。
- 危险操作（force push、历史改写、绕过保护分支）按 `confirm-first` skill 先向用户确认。
- **完成判定**：⑦ 全部达成（issue closed ∧ PR merged ∧ 特性分支已删 ∧ 验证全绿 ∧ §2.2 的⑨交付后义务已执行）才算「任务结束」。
