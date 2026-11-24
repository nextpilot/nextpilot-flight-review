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
| 建 PR | `POST /api/v5/repos/nextpilot/nextpilot-advance-plotscope/pulls` | JSON body：`head`/`base`/`title`/`body`/`close_related_issue`（**必须显式传 `0`**，见「issue 关闭由 AI 执行」） |
| 更新 PR 描述 | `PATCH /api/v5/repos/nextpilot/nextpilot-advance-plotscope/pulls/<n>` | JSON body：`body`（新描述全文，**整段替换**）；首行写 `Closes #IKxxxxx`；分支每新增提交都要刷新，多提交需全列 |
| 审批 PR | `POST /api/v5/repos/nextpilot/nextpilot-advance-plotscope/pulls/<n>/review` | body 带 `event=APPROVE`；成功返回 204 |
| 查 PR 状态 | `GET /api/v5/repos/nextpilot/nextpilot-advance-plotscope/pulls/<n>` | 看 `state`（`open`/`merged`/`closed`）、`mergeable`、**`close_related_issue`** |
| 合并 PR | `PUT /api/v5/repos/nextpilot/nextpilot-advance-plotscope/pulls/<n>/merge` | body 带 `merge_method=squash`（**参数名是 `merge_method`，不是 `merge_style`**）；**须先过 review + testers + test 三道门禁**（见下） |
| 指定测试人 | `POST /api/v5/repos/nextpilot/nextpilot-advance-plotscope/pulls/<n>/testers` | body 带 `testers=<用户名>`（复数）；成功返回 201 |
| 提交测试通过 | `POST /api/v5/repos/nextpilot/nextpilot-advance-plotscope/pulls/<n>/test` | body 带 `event=APPROVE`；成功返回 204 |
| 关闭 issue | `PATCH /api/v5/repos/nextpilot/issues/<num>` | **由 AI 显式执行**：表单参数必须带 `repo=nextpilot-advance-plotscope` + `state=closed`；只关目标 issue |
| 删文件（免强推） | `DELETE /api/v5/repos/nextpilot/nextpilot-advance-plotscope/contents/<path>` | body 带 `sha`/`branch`/`message`；清理误传文件用，绕过保护分支 |

## 平台踩坑

- **git 推送用户名 = 令牌所属账号**：`https://nextpilot:<TOKEN>@...` → 403 `The token username invalid`（令牌本身有效也没用）；改 `https://latercomer:<TOKEN>@...` 即成功。不确定所属账号先 `GET /api/v5/user?access_token=<TOKEN>` 看 `login`。
- **项目型仓库 issue 只走扁平路径**：`POST/PATCH /repos/nextpilot/issues`（仓库名放表单参数 `repo`）。标准路径 `POST/PATCH /repos/nextpilot/nextpilot-advance-plotscope/issues/<num>` → 404/「project or enterprise」——项目型仓库的 issue 编号是 `IKxxxxx` 标识串，不是纯数字。
- 关闭 issue **只能**用 `PATCH /repos/nextpilot/issues/<num>` + `repo=`。`PATCH /enterprises/nextpilot/issues/<num>` → 403「仓库级私人令牌无权」。
- `main` 是保护分支：**任何直推**（哪怕带令牌）都返回 `No permission to push this protected branch`。改动只能经 PR；清理误传文件用 contents API（见上表），不强推。
- PR 合并可能被门禁拦截：`未通过设置的审查`（需先 approve）或 `未通过设置的测试`（需先指定 tester）。遇此先走下面的三步合并顺序，不绕过。
- **合并的完整四步顺序**（本仓库三道门禁，缺一步都会被拦）：
  1. `POST /repos/nextpilot/nextpilot-advance-plotscope/pulls/<n>/review`（body 带 `event=APPROVE`）→ 204
  2. `POST /repos/nextpilot/nextpilot-advance-plotscope/pulls/<n>/testers`（body 带 `testers=<用户名>`，**字段名是 `testers` 复数**，写 `tester` 会返回 400 `testers is missing`）→ 201

     ⚠️ 这一步**必须用表单提交**，用 JSON body 会返回 400 `testers is invalid`：

     ```bash
     # 对：表单 → 201
     curl -s -X POST ".../pulls/<n>/testers" \
       -H "Authorization: token <TOKEN>" -d "testers=latercomer"
     # 错：JSON body → 400 {"messages":["testers is invalid"]}
     curl -s -X POST ".../pulls/<n>/testers" \
       -H "Content-Type: application/json" -d '{"testers":["latercomer"]}'
     ```
  3. `POST /repos/nextpilot/nextpilot-advance-plotscope/pulls/<n>/test`（body 带 `event=APPROVE`）→ 204
  4. `PUT /repos/nextpilot/nextpilot-advance-plotscope/pulls/<n>/merge`（body 带 `merge_method=squash`）→ 200

  三道门禁的报错分别是 405「未通过设置的审查」（缺第 1 步）、405「未通过设置的测试」（缺第 2 或第 3 步）。第 2 步是「指定测试人」、第 3 步是「测试通过」，**两者都要做**，只做其一时 `can_merge_check` 仍为 `False`。
- **合并参数名是 `merge_method`，不是 `merge_style`**：写 `merge_style=squash` / `merge_style=rebase` / `merge_style=fast-forward` 一律返回 405「当前仓库 Pull Request 已关闭 merge 合并，请使用其他合并方式」。正确写法是 `merge_method=squash`，返回 200 与 `merged: true`。
- **本仓库已关闭 merge 合并方式**：仓库「功能设置」只开放 squash 与 rebase。传 `merge_method=merge` 同样被 405 拦下。当前用 `merge_method=squash`。
- **squash 会把各提交的 `Refs:` footer 吃掉**：合并后 main 上是单个压缩提交，历史里查不到原始提交的 `Refs:`。追溯线索因此必须由 PR 描述的「提交清单」承载，commit footer 只作过程记录。同理不要为补 `PR:` footer 而 amend 已推送的提交。
- **issue 关闭由 AI 显式执行，不由平台自动关**：仓库设置已关闭「合并时自动关闭关联 issue」，建 PR 时仍显式传 `close_related_issue: 0`，避免仓库设置被改回后无声失效。该字段为 `1` 时，平台在 PR 合并时会把描述中**所有**带 `#` 的 issue ident 一并关闭——不只 `Closes #` 那一个，仅作引用、来源说明、评审裁定的 ident 同样会被连带关闭。为 `0` 时平台不关闭任何 issue，关闭动作由 AI 在合并后显式执行。

  **建 PR 时必传**：

  ```bash
  curl -s -X POST "https://gitee.com/api/v5/repos/nextpilot/nextpilot-advance-plotscope/pulls" \
    -H "Content-Type: application/json" \
    -d '{"access_token":"<TOKEN>","title":"...","head":"<分支>","base":"main",
         "body":"...","close_related_issue":0}'
  ```

  **PR 描述里的 issue 编号可以照常写**：目标 issue 写 `Closes #<目标>`，其他 issue 用 `#<编号>` 正常引用、出处、说明关系。追溯信息不再需要为了规避连带关闭而删减。

  **建 PR 后必须回读校验**：`GET /api/v5/repos/nextpilot/nextpilot-advance-plotscope/pulls/<n>` 看 `close_related_issue` 是否为 `0`。为 `1` 说明开关未生效，**先 PATCH 修正**（`PATCH .../pulls/<n>` 带 `close_related_issue: 0`）再合并；若修正后仍为 `1`，则本次 PR 的描述退回「只保留目标 issue 的 `#`」的严格写法，合并后再逐个 `GET` 描述里引用过的 issue，确认其 `state` 仍为 `open`，被连带关闭的用 `PATCH .../issues/<编号>` + `state=open` 恢复。

  **关闭动作的唯一执行路径**（AI 在 PR 合并后执行，只关目标 issue）：

  ```bash
  # 只关目标 issue；表单参数必须带 repo=
  curl -s -X PATCH "https://gitee.com/api/v5/repos/nextpilot/issues/<目标编号>" \
    --data-urlencode "access_token=<TOKEN>" \
    --data-urlencode "repo=nextpilot-advance-plotscope" \
    --data-urlencode "state=closed"
  ```

  **合并后必须复查**：目标 issue 的 `state` 确认为 `closed`；描述里引用过的其他 issue 确认仍为 `open`。
- **源分支自动删除看仓库设置**：设置「合并后删除源分支」开启才会自动删；未开启时远端分支仍在，需手动 `git push origin --delete`。本地分支任何情况下都手动删。
- **删远端分支用 git，不用 API**：`DELETE /api/v5/repos/<org>/<repo>/branches/<分支名>` 返回 405 `Not Allowed`。改用 `git push <远端URL> --delete <分支名>`，实测正常。
- **`review` / `test` 成功时返回 204 无 body**：不要对这俩响应做 JSON 解析，否则会误判成失败。判成功看 HTTP 状态码（204）。
- **截图走图床，不进业务仓**。issue 评论框的「上传图片」只认网页登录 cookie，令牌调用返回 401；v5 API 无开放图床上传端点。改用独立图床仓库：contents API 上传后拿 `download_url` 外链（见 `issue-image-hosting.md`）。禁止 `git add` 截图进业务仓。

## PR 合并后的闭环校验（PM 执行）

平台能力：建 PR 时传 `close_related_issue: 0`，平台在合并时**不自动关闭**任何 issue；源分支是否自动删除取决于仓库设置「合并后删除源分支」。

```text
loop（每隔一段时间，设上限避免无限等待）：
    GET PR 状态
    if state == "merged":
        POST /repos/nextpilot/nextpilot-advance-plotscope/issues/<num>/comments
            # PM 回填验收结论：PR 号、提交号、变更摘要、
            # 测试结果（数字）、验收标准逐条结果、遗留项
        GET 目标 issue 详情
        if state != "closed":              # 关目标 issue
            PATCH /repos/nextpilot/issues/<目标编号>  { repo, state=closed }
        GET 描述里引用过的其他 issue
        if 某个被误关:                      # 恢复
            PATCH /repos/nextpilot/issues/<该编号>  { repo, state=open }
        GET 远端分支列表
        if 源分支仍存在:                    # 仓库未开自动删才手动删
            git push origin --delete <特性分支>
        git branch -D <特性分支>           # 本地分支始终手动删
        结束
    elif state == "closed":   # PR 被关但未合并
        PM 评论说明原因；issue 保持 open 或重开；通知 Dev 是否继续
    else:                     # open
        继续等待；若有 CI/review 未过则先修复
```

- 校验口径以 `GET` 返回的**实际状态**为准，不以推断代替校验。
- 合并后另需确认 PR 描述与最终提交一致（多提交逐条列出），描述过期则先 PATCH 再收尾。
- 若 CLI 会话无法持续轮询，交付说明中明确写下「PR 号 + 待合并后校验并关闭 IKXxxxx」待办，由下次会话接续，不能默认已完成。
