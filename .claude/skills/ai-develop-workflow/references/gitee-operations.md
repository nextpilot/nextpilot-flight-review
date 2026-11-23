# Gitee 平台操作

本仓库是**企业 `nextpilot` 下的仓库**，`main` 为**保护分支**，普通个人仓库的 API 路径大多不可用。只在需要操作 Gitee 时读本文。

## 认证与令牌（三种方式，均已实测）

令牌是**仓库级私人令牌**，所属账号 `latercomer`。**令牌运行时从用户获取，严禁写入仓库任何文件**（包括 skill 与 reference）。

### ① git 推送：URL 内嵌令牌

```bash
# 用户名必须是"令牌所属账号"，不是仓库属主！
git push https://latercomer:<TOKEN>@gitee.com/nextpilot/nextpilot-advance-plotscope.git <refspec>
```

⚠️ 写 `nextpilot:<TOKEN>` 会报 403 `The token username invalid`——即令牌有效，用户名对不上账号也不行（实测踩坑）。拿不准所属账号时先查：`GET /api/v5/user?access_token=<TOKEN>` 返回的 `login` 字段就是正确用户名。

### ② API 调用：body/query 带 `access_token`

所有已验证端点均用此形式，最稳：

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

### ③ header 形式

部分端点可用，不如 ② 可靠，不推荐首选：

```bash
curl -s -H "Authorization: token <TOKEN>" "https://gitee.com/api/v5/repos/..."
```

所需 scope：`issues`（建/关 issue）、`notes`（评论）、`pull_requests`（PR）。企业级操作（`/enterprises/...` 路径）仓库级令牌无权，一律走下面的 repos 扁平路径。

## 关键 API 端点（均已验证）

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

## 平台踩坑

- **git 推送用户名 = 令牌所属账号**：`https://nextpilot:<TOKEN>@...` → 403 `The token username invalid`（令牌本身有效也没用）；改 `https://latercomer:<TOKEN>@...` 即成功。不确定所属账号先 `GET /api/v5/user?access_token=<TOKEN>` 看 `login`。
- **项目型仓库 issue 只走扁平路径**：`POST/PATCH /repos/nextpilot/issues`（仓库名放表单参数 `repo`）。标准路径 `POST/PATCH /repos/nextpilot/nextpilot-advance-plotscope/issues/<num>` → 404/「project or enterprise」——项目型仓库的 issue 编号是 `IKxxxxx` 标识串，不是纯数字。
- 关闭 issue **只能**用 `PATCH /repos/nextpilot/issues/<num>` + `repo=`。`PATCH /enterprises/nextpilot/issues/<num>` → 403「仓库级私人令牌无权」。
- `main` 是保护分支：**任何直推**（哪怕带令牌）都返回 `No permission to push this protected branch`。改动只能经 PR；清理误传文件用 contents API（见上表），不强推。
- PR 合并可能被门禁拦截：`未通过设置的审查`（需先 approve）或 `未通过设置的测试`（CI 门禁）。遇此先排查 review 与 status，不绕过。
- **不要把调试/截图图片传进仓库**。评论框「上传图片」是图床（存服务器、不进仓库），但只认网页登录 cookie，令牌调用返回 401；v5 API 无开放图床上传端点。需要内嵌图片时用网页登录后上传或外链，禁止 `git add` 截图。

## PR 合并后关闭 issue（PM 执行）

```text
loop（每隔一段时间，设上限避免无限等待）：
    GET PR 状态
    if state == "merged":
        POST /repos/nextpilot/nextpilot-advance-plotscope/issues/<num>/comments
            # PM 回填验收结论：PR 号、提交号、变更摘要、
            # 测试结果（数字）、验收标准逐条结果、遗留项
        PATCH /repos/nextpilot/issues/<issue编号>  { repo, state=closed }   # PM 关闭 issue
        git push origin --delete <特性分支>；git branch -D <特性分支>        # Dev 或 PM 删除分支
        结束
    elif state == "closed":   # PR 被关但未合并
        PM 评论说明原因；issue 保持 open 或重开；通知 Dev 是否继续
    else:                     # open
        继续等待；若有 CI/review 未过则先修复
```

- 关闭前确认 PR 确实 `merged`，**不要根据"已提交 PR"就提前关闭**。
- 项目型 issue 不一定被 `Closes` 自动关闭；必须由 PM 核实并手动关闭。
- 若 CLI 会话无法持续轮询，交付说明中明确写下「PR 号 + 待合并后由 PM 关闭 issue IKXxxxx」待办，由下次会话接续，不能默认已完成。
