# issue 配图：图床使用

## 为什么要图床

Gitee issue 的 API **不支持上传附件**，AI 无法把截图直接塞进 issue。而把图片提交进业务仓库会污染代码仓（二进制文件、PR 体积膨胀、review 时看图改代码）。

约定：issue 讨论用的截图统一上传到独立图床仓库，业务仓库只留代码与文档配图。

| 用途 | 位置 |
|---|---|
| issue 讨论、PR 描述里的临时截图 | 图床 `nphelper/issue-image-hosting` |
| 文档正文配图、README 图示 | 业务仓库 `docs/screenshots/` |

## 仓库与目录约定

仓库：`https://gitee.com/nphelper/issue-image-hosting`（public，默认分支 `master`）

```text
<项目名>/issue/<issue编号>-<序号>-<短描述>.png
```

例：

```text
nextpilot-advance-plotscope/issue/IKXGYH-01-bare-shell-layout.png
nextpilot-advance-plotscope/issue/IKXGYH-02-activity-bar.png
```

- 项目名：Gitee 仓库名去掉组织前缀，本项目为 `nextpilot-advance-plotscope`
- issue 编号：短编号（如 `IKXGYH`），不是数字编号
- 序号：两位，从 `01` 起，同一 issue 内递增
- 短描述：小写连字符英文短语，说明图的内容

## 在 issue 中引用

用 raw 直链：

```markdown
![裸壳初始布局](https://gitee.com/nphelper/issue-image-hosting/raw/master/nextpilot-advance-plotscope/issue/IKXGYH-01-bare-shell-layout.png)
```

直链不生效时（部分平台的 markdown 渲染器拒绝跨域图片），退化为文件页面链接：

```markdown
[IKXGYH-01-bare-shell-layout.png](https://gitee.com/nphelper/issue-image-hosting/blob/master/nextpilot-advance-plotscope/issue/IKXGYH-01-bare-shell-layout.png)
```

引用前必须自己拉一次 raw 链接确认返回 200 且 `content-type` 为 `image/*`，不能假定已上传就能显示。

## 上传截图

### 方式一：contents API（推荐，单张）

```bash
B64=$(base64 -w0 /tmp/shot.png)
curl -s -X POST "https://gitee.com/api/v5/repos/nphelper/issue-image-hosting/contents/<路径>" \
  -H "Authorization: token <令牌>" \
  -H "Content-Type: application/json" \
  -d "{\"content\":\"$B64\",\"message\":\"<提交说明>\",\"branch\":\"master\"}"
```

返回里的 `content.download_url` 就是 raw 直链。

### 方式二：git 推送（多张时）

```bash
cd /tmp/img-host
git clone https://oauth2:<令牌>@gitee.com/nphelper/issue-image-hosting.git
cp /tmp/shot*.png nextpilot-advance-plotscope/issue/
git add -A && git commit -m "docs: 补充 IKXGYH 界面截图"
git push
```

一次多张图用一个提交，比逐张 API 调用少很多次提交。

### 删图

```bash
curl -s -X DELETE "https://gitee.com/api/v5/repos/nphelper/issue-image-hosting/contents/<路径>" \
  -H "Authorization: token <令牌>" \
  -H "Content-Type: application/json" \
  -d '{"message":"<提交说明>","sha":"<上一步返回的 sha>","branch":"master"}'
```

`DELETE` 必须带文件的 `sha`，否则 400。

## 上传前的检查

1. **打码**：token、内网地址、真实业务数据、用户名邮箱必须遮挡。
2. **裁剪**：只截相关区域，不要整屏。宽度压到 1600px 以内。
3. **压缩**：用 `pngquant` 或 `optipng`，单张控制在 500 KB 内。
4. **去重**：同一张图不重复上传；引用已存在的图直接给链接。
5. **不越界**：图床只放 issue 专用截图，文档配图仍归业务仓。

## 纪律

- 图床必须保持 **public**，否则外链图片加载不出来。
- 分支名写进 raw 链接，换默认分支时所有旧链接都要跟着改。
- 图床仓库的提交不要求 `Refs:`（它不是业务代码仓），但提交说明仍写清归属 issue。
- 上传图床不改业务仓库，不因此建 PR；但**引用了图的 issue/PR 描述改动属于业务仓改动**，仍走正常 PR 流程。
- 不把图床令牌、临时文件路径写进 issue 正文。

## 排错

| 现象 | 原因与处理 |
|---|---|
| 图片不显示 | raw 链接返回 302 跟随后是否 200；仓库是否 public；分支名是否与链接一致 |
| 404 | 路径大小写或目录层级不对；先 `GET /repos/nphelper/issue-image-hosting/contents/<目录>` 列目录确认 |
| API 上传返回 400 | JSON 格式错或 `branch` 不存在；分支用 `master` |
| DELETE 返回 400 | 缺 `sha`；用上传时返回的 `content.sha` |
| 图片过大加载慢 | 未压缩；用 pngquant 重新压 |
