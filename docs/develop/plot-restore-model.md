# Plot 恢复模型：存档语义与恢复链

> 依据：追平方案 #IKXD18 v3（第2仗 !34/!38/!39，第3仗 !41/!44/!50）。
> 本文是随码设计文档（Q-2），描述布局/视图状态的存档口径、恢复链时序与晚到数据绑定。
> ULP 不在本文范围内：断言口径统一为"相对秒视口往返精度（1e-9 级）+ 不被覆盖"，无 ULP 补丁。

## 单一真值与单位口径

| 层     | 真值                          | 说明                                                              |
| ------ | ----------------------------- | ----------------------------------------------------------------- |
| 数据层 | `TimeNs = int64_t`（整数 ns） | dataset 时间轴单一真值；Table 只存 schema 元数据 + 首尾时间       |
| 绘图层 | 相对秒（double）              | `t0 = firstTimeNs / 1e9`；曲线/视口/游标全在相对秒轴上运算        |
| 存档层 | 相对秒（double）              | `saveViewState` 的 `x`/`y` 区间、参考线 `referenceT` 均为相对轴值 |

推论：**offset（曲线 X 偏移）只叠加在绘图副本上，不进存档**——存盘视口不因 offset 漂移
（测试 8c）。存相对轴值的额外收益：天然抗时间基准偏移，恢复时无需知道绝对 epoch。

## 恢复链时序（applyDocumentSpec）

```text
preflightDocumentSpec   # 开档前体检：origin 可满足性/通道缺口清单（诊断，不阻断）
  └─ dropPendingBinds   # 新文档接管 pending 队列
  └─ 逐页逐格：
       makeCell → setCellType
       先绑曲线（读回 style.series[].color 显式色 + .visible 显隐）
       → restoreViewState(c.state)        # 最后：视口/图例四角/参考线/左右轴/变换
       → 绑不上 → PendingBind 挂起（等待晚到数据）
  └─ m_history->noteRestored()              # undo 基线对齐，恢复期动作流被抑制
```

时序是正确性的核心：**先绑曲线再 restoreViewState**。绑曲线触发 pushToPlot
（旧实现会 `setXRange(m_t0, m_t1)` 用数据全范围覆盖视口）；恢复顺序反过来，
存档视口必然被冲掉。第3仗 3b 在 AxesView 加 `m_restoreInProgress` 作用域守卫 +
`m_preserveRangeOnNextPush` 一次性标志，恢复链内所有 pushToPlot 都不覆盖视口。

## 状态通道：单一通道，无第二真值

所有画布视图状态经 **一条通道** 存取：

```text
AxesView::saveViewState  ──►  CanvasSpec.state  ──►  AxesView::restoreViewState
（fastplot Axes::saveState 的全部键 + seriesAxes + seriesTransforms + axisTransform）
```

`legendCorner`（图例四角）、`legendVisible`、`referenceT`（参考线相对轴值）、
视口区间全部走这同一条链。缺键语义逐键定义：视口/显隐缺键不动现状；
`legendCorner` 缺键回落默认右上（0）；`referenceT` 缺键不动现状（旧存档兼容）。

## 晚到绑定（布局先到，数据后到）

场景：布局档先载入（origin 未加载/数据集替换/换文件），画布空态挂起。

- `PendingBind`：page/cell/origin/channels + 存盘色/显隐/视口快照；挂在工作区。
- 触发：`sourceAdded / sourceChanged / datasetAdded` 信号 → `tryBindPending`。
- demand reference（PR-B）：挂起期间对 origin 持订阅需求引用（quiet stream 不被裁）；
  绑定成功 `releaseOrigin`。
- 绑定完成后回放存档视口 + 显隐（`visibleByChannel`），体验等同"数据一开始就在"。

## 数据集替换与重绑（PR-C）

- `rebindStaleCurves`：曲线 key=`datasetId|table|column` 中 datasetId 失联时，
  按 `table.column` 三路解析——旧 id 仍有效→不动；唯一携带者→静默重绑；
  多个携带者→取登记序第一个并在诊断面板报告。
- `chooseDatasetFor`：多数据集并存时按通道可满足性打分选最佳拟合，并列取登记序第一个。
- 显式色与显隐在重绑后保留（身份不随数据集消失而丢，kept 语义）。

## 全工作区快照 undo（第3仗 3b）

- `WorkspaceHistory`：快照 = `toDocumentSpec()`（页结构 + 曲线/样式/视口状态），栈上限 64。
- 体验标准：变更后安静 200ms 结算一步（连续手势 = 1 步 undo）；与基线相同的快照丢弃；
  恢复期 `setRestoring` 防误发布。
- **复用 applyDocumentSpec 恢复链**，不自建命令抽象、不复用 commands/ 意图目录——
  undo 的"重放"就是一次文档恢复，恢复链的正确性由同一组回归网保证。
- 视口 zoom/pan 保留画布局部 `ViewState` 栈（`plot.view.undo`），与全局快照栈不混栈。

## 回归锚点

| 不变量                                  | 测试（tests/test_plot_io.cpp） |
| --------------------------------------- | ------------------------------ |
| 视口不被绑曲线覆盖（preserveRange）     | 8a                             |
| 显式色稳定保留、不被 palette 重分配覆盖 | 8b                             |
| offset 不进存盘视口                     | 8c                             |
| 相对秒视口往返精确                      | 9b                             |
| 晚到绑定 + 视口/显隐回放                | 10                             |
| 多数据集最佳拟合 + 重绑                 | 11/12                          |
| 全文档 round-trip 全等 + apply 幂等     | 16                             |
| 全局 undo 步数/拆分回退                 | 14                             |
| visibility 五路径三方一致               | 13                             |
