# 画布 Overlay 脏标记分层契约

> 依据：追平方案 #IKXD18（第1仗，!29 / !64）。本文是对"第1仗地基"的随码设计文档（Q-2），
> 描述 `libs/fastplot` 画布的绘制分层、脏标记传播规则与性能不变量。
> **定位声明：这是我们的 QPainter 自研分层方案，不是 PJ4 GPU retained layer 的等价物**——
> PJ4 的分层服务它自己的渲染后端；我们只对标其"游标移动不重算稳态内容"的体验，不移植其机制。

## 分层与绘制顺序

`Axes::paintEvent` 每帧按以下顺序合成（下→上）：

| 层 | 内容 | 缓存策略 | 脏标记 |
|----|------|----------|--------|
| 静态层 | 网格、轴、标题、边框 | `m_staticCache`（QPixmap） | `m_staticDirty`（视口/主题/尺寸变化时置位） |
| 曲线层 | 包络/折线/散点 | 无整层缓存；**聚合结果**按（数据 revision, 视口, 列数）键缓存于 `LodCache` | 视口或数据变化触发聚合重算 |
| 稳态叠加层 | StateBand / Marker / Bookmark / A/B 测量 | `m_overlayCache`（透明 QPixmap） | `m_overlayDirty` + `m_overlayDirtyLayers` 位掩码 |
| 游标组 | Crosshair / CursorReadout / 橡皮筋 | 不缓存，每帧实时画 | 每帧 |

缓存的是稳态层的**绘制结果**，跳过的是它的重算，而不是跳过把它显示出来——
所以稳态组和游标组每帧都要"贴/画"一遍（`paintEvent` 用 staticCache 覆盖全画布）。

## 脏标记传播规则

1. **置位即延迟重绘**：稳态层内容变更（书签增删/拖动、marker 生成、A/B 开关、
   状态带数据变化）调用 `invalidateOverlay(layer)`：置 `m_overlayDirty` 与对应层位掩码，
   然后 `update()`。不直接画。
2. **合成时重建**：下一帧 `paintOverlay` 发现 `m_overlayDirty` → 重建透明缓存
   （按位掩码只重画脏层，其余层从旧缓存语义上已包含）→ 清标记 → `++m_overlayCacheRebuilds`。
3. **游标组永远不进缓存**：crosshair/读数框每帧变化，画它们的成本低于缓存失效成本。
4. **视口变化级联**：`setXRange/setYRange` → `relayout()` → `invalidateStatic()` +
   `m_overlayDirty`（叠加层带里的时间刻度位置依赖视口）。
5. **尺寸/DPR 变化**：缓存按 `size() * devicePixelRatioF()` 校验，尺寸不符即重建。

## revision 短路（契约层配套）

与画布脏标记正交的另一处防重复开销：`PersistentStateEntry`（`libs/context`）支持可选
`revision` 回调——`collect()` 在版本未变时**复用上次序列化结果**（`m_serializeCache`），
跳过昂贵的 serialize（如导出全部曲线样式）。约束：serialize 的输出必须只依赖 revision
覆盖的内容。诊断计数 `lastCacheHits()`。

## 性能不变量（测试锚点）

| 不变量 | 测试 | 断言 |
|--------|------|------|
| 游标移动稳态层零重建 | `test_fastplot.cpp` 脏调度测试 | 20 次游标移动 `overlayCacheRebuilds` delta == 0 |
| 稳态/视口变化正确重建 | 同上 | 置脏后下一帧 delta == 1 |
| 游标移动 LOD 计数不变 | 同上（LodCache 视口键） | 视口不变时聚合不重算 |
| NaN gap 断线（!64） | `test_fastplot.cpp` 6c | 三种缺测模式像素级断线 + 无假 gap |

## 实测数据

见 [plot-backends-benchmark.md](plot-backends-benchmark.md) 的"追平优化实测"一节
（20 曲线拖拽 fps、游标移动帧耗、稳态层重建计数，优化前后同口径对比）。

## 已知的下一步优化空间（不在追平范围内）

实测表明每帧成本由 **paintCurves**（每帧重画 20 条包络，Qt raster 逐线）主导；
稳态叠加层缓存的墙钟收益在叠加层内容轻量时不显著。若未来要继续提帧率，
方向是给曲线层引入视口键控的像素缓存（pan 期间失效、游标移动期间命中）——
这是第1仗分层地基上的自然延伸，不属于追平范围。
