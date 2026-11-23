# 绘图后端性能与功能对比

> 本文是 `libs/fastplot` 自绘方案（Rust min/max LOD + 折线渲染）的选型依据。
> 文中的"自绘 `PlottingCanvas`"是当时基准原型名，对应现在的 fastplot 绘图部件与
> `AxesView` 画布宿主；结论未变——主画布自绘，不换通用绘图库。

## 结论

1. **自绘 `PlottingCanvas` 仍是当前大点数遥测场景的最优解，但优势已经没有 Debug 数据看起来那么大**。10 条曲线 × 100 万点，Release 整帧 34.5 ms（29.0 fps）；QCustomPlot 自适应采样 39.2 ms（25.5 fps），Qwt 默认 331.2 ms（3.0 fps）、开启激进过滤后 109.6 ms（9.1 fps）。
2. **QCustomPlot 默认自适应采样是三方库里最接近可用的方案**。它在百万点能保持约 25 fps，但代价是曲线细节和尖峰包络可能被平滑；关闭自适应后 10 条 × 100 万点 763 ms/帧，不能用于原始遥测直绘。
3. **Qwt 功能完整、导出/打印成熟，并且有 `FilterPointsAggressive` 抽稀接口**，但默认没开。默认 10 条 × 10 万点 76.0 ms/帧；开启后 18.9 ms，100 万点 109.6 ms，仍比自绘 34.5 ms 慢约 3.2 倍。
4. **Qt Charts 不作为遥测主画布候选**。它适合仪表盘和常规业务图，不提供百万点级抽稀与时间窗交互；本轮未做实测，只做功能对照。
5. **JKQtPlotter 完整版功能面很全，但大数据性能不达标**。10 条 × 10 万点已需 1407.8 ms/帧（0.7 fps），默认线压缩仍无法替代列桶 LOD；`JKQTFastPlotter` 未测。
6. 现阶段不建议替换自绘画布。可借鉴 Qwt 的 PDF/SVG 导出和 QCustomPlot 的交互/图层组织，但核心曲线渲染继续保留 Rust min/max LOD + `drawPolyline`。

## 测试环境

| 项 | 值 |
|---|---|
| 机器 | 云主机，2 vCPU AMD EPYC 9Y24，4 GB RAM |
| Qt | 6.2.4，`QT_QPA_PLATFORM=offscreen` |
| 编译 | Release：自绘 C++ / QCustomPlot 为 `-O3`，Qwt 为 `-O2` |
| 渲染 | 纯软件栅格，无 GPU |
| 视口 | 1280 × 720 |
| 数据 | 10 条曲线，每条 10 万 / 100 万点，600 s 时间跨度 |
| 计时 | 预热 2 帧后，统计 5–15 帧；包含 backend 重绘 + `QWidget::render` 到 QImage |
| 自绘配置 | Rust `ps_lod_minmax_many`，每像素 min/max 桶；曲线 `drawPolyline` 关闭抗锯齿 |
| QCustomPlot | 2.1.1，默认自适应采样；另测 `--adaptive 0` 原始模式 |
| Qwt | 6.3.0，`QwtPlotCurve::setSamples`；默认 `ClipPolygons | FilterPoints`，另测 `FilterPointsAggressive` |

> 早期一次数据是在 `CMAKE_BUILD_TYPE=Debug` 下跑的，QCustomPlot 没有开优化，Qwt 却是 `-O2`，口径不一致，已作废。下面全部是 Release 数据。

这是软件栅格基准，绝对值会随 CPU 变化；重点看同一环境下的相对倍率。

其他 Qt/C++ 候选，本轮未实测，只做定位说明：

- **JKQtPlotter**：LGPL 2.1，功能型科学绘图库，支持曲线、柱状、统计、误差棒、LaTeX 标注等。完整版默认关闭线压缩，10 条 × 10 万点 1407.8 ms/帧；手动开压缩后 541.7 ms/帧，仍不适合百万点。`JKQTFastPlotter` 是另一个精简后端，本轮未测。
- **KPlotting / `KPlotWidget`**：KDE Frameworks 的通用 QWidget 绘图组件，LGPL，二维折线/散点为主，API 轻量，没有百万点 LOD 和遥测时间轴交互。
- **KDChart**：KDAB 的 Qt 图表库，MIT，支持笛卡尔/极坐标/三元图、折线/柱状/饼图等，线图有 `PlotterDiagramCompressor`，但它不是飞控需要的每像素 min/max 包络 LOD，也没有 A/B 测量、视频游标这类遥测交互。
- 如果指的是 **Kst**，那是 KDE 的实时绘图应用，不是可嵌入的 Qt 绘图库，不参与本轮选型。

## 性能结果

### 10 条曲线 × 10 万点，全时间窗

| 后端 | 首帧 ms | 中位 ms | P95 ms | 中位 fps |
|---|---:|---:|---:|---:|
| 自绘画布 | 10.1 | **6.1** | 6.9 | **162.9** |
| QCustomPlot（自适应） | 23.9 | 20.8 | 21.1 | 48.1 |
| Qwt（默认） | 79.2 | 76.0 | 76.6 | 13.2 |
| Qwt（FilterPointsAggressive） | 26.3 | **18.9** | 18.9 | **53.0** |
| JKQtPlotter（默认，压缩关） | 1400.6 | 1407.8 | 1434.6 | 0.7 |
| JKQtPlotter（手动开压缩） | 560.4 | 541.7 | 543.3 | 1.8 |

### 10 条曲线 × 100 万点，全时间窗

| 后端 | 首帧 ms | 中位 ms | P95 ms | 中位 fps |
|---|---:|---:|---:|---:|
| 自绘画布 | 38.4 | **34.5** | 34.6 | **29.0** |
| QCustomPlot（自适应） | 45.1 | 39.2 | 41.3 | 25.5 |
| Qwt（默认） | 342.6 | 331.2 | 333.0 | 3.0 |
| Qwt（FilterPointsAggressive） | 109.4 | **109.6** | 110.4 | **9.1** |

### 10 条曲线 × 10 万点，只看 1% 时间窗

| 后端 | 首帧 ms | 中位 ms | P95 ms | 中位 fps |
|---|---:|---:|---:|---:|
| 自绘画布 | 5.1 | **1.2** | 1.4 | **827.7** |
| QCustomPlot（自适应） | 5.1 | 1.9 | 1.9 | 540.1 |
| Qwt（默认） | 38.9 | 33.6 | 37.9 | 29.7 |
| Qwt（FilterPointsAggressive） | 28.0 | **24.4** | 24.8 | **41.1** |

### QCustomPlot 自适应采样开关

| 数据 | 自适应 | 中位 ms | 中位 fps |
|---|---|---:|---:|
| 10 × 10 万 | 开 | 20.8 | 48.1 |
| 10 × 10 万 | 关 | 95.5 | 10.5 |
| 10 × 100 万 | 开 | 39.2 | 25.5 |
| 10 × 100 万 | 关 | 763.4 | 1.3 |

自适应采样把绘制点降到屏幕列级别，代价是曲线细节和包络可能被平滑；自绘 LOD 保留每列 min/max，既降点又保留尖峰。Release 下 QCustomPlot 在百万点已经接近自绘，但视觉保真度和时间窗语义仍是自绘占优。

### 口径辨析：全量输入 vs 预抽稀输入

上面的表是“后端自己处理全量数据”。自绘的优势来自每帧在 `paintEvent` 里调用 `ps_lod_minmax_many`，把每条 10 万/100 万点压到约 1280 个 `(t,min,max)` 桶，再按桶中点画折线；这个抽稀本身在计时内，不是预先算好的缓存。Qwt/QCustomPlot/JKQt 的 `setSamples`、`setData`、`addCopiedColumn` 拷贝发生在 setup，计时不含。

为了单独看“纯绘图”成本，加了一组预抽稀输入：所有后端都只拿 2×1280 点/曲线（10 条，共 25.6k 点），数据形状一致。

| 10 条 × 10 万点 | 全量输入中位 ms | 预抽稀输入中位 ms |
|---|---:|---:|
| 自绘画布 | **6.1** | **2.9** |
| QCustomPlot（自适应） | 20.8 | 19.2 |
| Qwt | 76.0 | 9.3 |
| JKQtPlotter（开压缩） | 541.7 | 581.7 |

这组对照说明：

- **Qwt 和 QCustomPlot 的“全量慢”主要来自输入点数**。喂 25.6k 点后 Qwt 从 76.0 ms 降到 9.3 ms（激进过滤 9.0 ms）；QCustomPlot 仍有约 19 ms 的固定重绘开销，所以只降到 19.2 ms。
- Qwt 的 `FilterPointsAggressive` 是每段像素列保留 first/min/max/last，语义接近自绘 LOD，但它是默认关闭的可选属性；开启后 1M 点仍为 109.6 ms。
- **JKQtPlotter 的问题不只是数据量或拷贝**。即使只喂 25.6k 点，仍要 581.7 ms，说明完整版每帧重建 `QPolygonF`、做 `JKQTPSimplifyPolyLines` / `JKQTPClipPolyLines` 逐段裁剪、再走完整 plot 重绘的管线本身很重。
- **自绘不是“免费抽稀”**。`ps_lod_minmax_many` 每帧扫全量数据，100 万点 × 10 条仍要 34.5 ms；它快在“扫描一次、输出每列 min/max”，不是跳过计算。
- 如果比较“已经抽稀后的绘图后端”，自绘仍最快，Qwt 次之，QCustomPlot 有固定开销，JKQtPlotter 不可用；如果比较“从全量遥测到成图”，自绘与 Qwt 激进过滤的差距是 34.5 ms vs 109.6 ms。

> 说明：JKQtPlotter 默认 `useNonvisibleLineCompression=false`，所以默认 1407.8 ms；手动开启后 541.7 ms。预抽稀那组是在开启压缩下测的，仍有 581.7 ms，进一步说明其瓶颈不只在输入点数。

### 各后端的拷贝发生在哪

| 后端 | 初始化/装载 | 每帧绘制 |
|---|---|---|
| 自绘画布 | `addGeneratedSeries` 用 `QVector` 隐式共享，通常不深拷贝；内核指针模式直接引用 | 分配 `lod` 缓冲，`ps_lod_minmax_many` 输出约 1×像素列的 `(t,min,max)` 三元组；再按 `(min+max)/2` 构造 `QPolygonF` 画折线 |
| QCustomPlot | `setData` 把点深拷贝进 `QVector<QCPGraphData>` | `getLines` 每帧构造 `QVector<QCPGraphData> lineData` 和 `QVector<QPointF> lines`；自适应开启时只处理抽样子集，关闭时是全量 |
| Qwt | `setSamples` 走 `QwtPointArrayData`，x/y 数组隐式共享 | 默认每帧把可见范围全量点变换成 `QPolygonF`；`FilterPointsAggressive` 在映射前按像素列块保留 first/min/max/last |
| JKQtPlotter | `addCopiedColumn` 深拷贝两列数据 | 每帧构造完整 `QPolygonF`，再做 `JKQTPSimplifyPolyLines`、`JKQTPClipPolyLines`，中间还会产生新的多边形副本 |

结论：

- **自绘不是不拷贝**，而是只拷贝“抽稀后的输出”，不每帧复制全量原始点。
- **Qwt 默认每帧把全量点变成绘制用 `QPolygonF`**；开启 `FilterPointsAggressive` 后先列块过滤再映射，但仍保留约 4 点/列。**JKQt 每帧构造完整多边形，还会多做简化/裁剪副本，所以更重。**
- **QCustomPlot 自适应模式只处理子集**，但仍有每帧 `lineData`/`lines` 构造开销，因此预抽稀后仍有约 19 ms。
- 初始化拷贝在本次计时外；每帧构造绘制缓冲在计时内。两者不能混为一谈。

## 功能对比

| 能力 | 自绘 `PlottingCanvas` | Qwt 6.3 | QCustomPlot 2.1 | Qt Charts 6.2 | ImPlot | KPlotting | KDChart |
|---|---|---|---|---|---|---|---|
| Qt Widget 原生嵌入 | 是 | 是 | 是 | 是 | 否，需 OpenGL 上下文 | 是 | 是 |
| 2D 折线 / 散点 | 是 | 是 | 是 | 是 | 是 | 是 | 是 |
| 百万点自动 LOD | **Rust min/max 每列桶** | `FilterPointsAggressive`（first/min/max/last，默认关） | 自适应采样，非 min/max 包络 | 无 | GPU 直绘，无 CPU LOD | 无 | 有压缩器，非 min/max 包络 |
| 多 Y 轴 | 是（左右轴） | 是 | 是 | 是 | 是 | 有限 | 是 |
| 时间轴 / 时间窗 | 是，飞控时间语义 | 通用数值轴 | 通用数值轴 | 通用数值轴 | 通用数值轴 | 通用数值轴 | 通用数值轴 |
| 平移 / 滚轮缩放 | 是 | 是（Zoomer/Panner） | 是 | 是 | 是 | 需自建 | 是 |
| 框选缩放 | 是（右键状态机） | 可配 Zoomer | 是 | 是 | 是 | 需自建 | 需自建 |
| A/B 测量 | 是 | 需自建 | 需自建 | 需自建 | 需自建 | 需自建 | 需自建 |
| 时间书签 | 是 | 需自建 | 需自建 | 需自建 | 需自建 | 需自建 | 需自建 |
| 实时跟随 / 视频游标 | 是 | 需自建 | 需自建 | 需自建 | 需自建 | 需自建 | 需自建 |
| 图例交互 | 是 | 是 | 是 | 是 | 是 | 有限 | 是 |
| 3D / 轨迹 / GPS 地图 | 是（轨迹 3D、GPS 地图、曲面） | 无 | 无 | 无 | ImPlot3D 单独项目 | 无 | 无 |
| 多画布 / 布局持久化 | 是 | 需自建 | 需自建 | 需自建 | 需自建 | 需自建 | 需自建 |
| 脚本/派生曲线 | 是 | 需自建 | 需自建 | 需自建 | 需自建 | 需自建 | 需自建 |
| PNG 导出 | 是 | 是 | 是 | 是 | 截图 | 是 | 是 |
| PDF / SVG 导出 | 否 | **是** | PDF 是，SVG 需额外 | PDF 是 | 否 | 否 | PDF/SVG 需额外 |
| OpenGL 加速 | 否 | 可选 | 可选 | 可选系列 | 是 | 否 | 否 |
| 许可证 | 项目自有 | Qwt License | GPL / 商业 | LGPL / 商业 | MIT | LGPL | MIT |
| 维护成本 | 高，自研交互全包 | 中，成熟但需适配 | 中，单文件易集成 | 低，但能力弱 | 高，非 Qt 原生 | 中，KDE 依赖 | 中，图表能力偏业务 |

## 建议

- 主画布继续用自绘：它的优势不只是帧率，而是 LOD 语义、飞控时间交互、书签/测量/视频游标和多画布都长在一起，换成通用库会丢掉这些。
- QCustomPlot 可以作为可选分析视图，默认开自适应采样；不要为了“保留全部点”关闭自适应，百万点会掉到 1 fps 级。
- 需要 PDF/SVG 报告时，可单独引入 Qwt 的 `QwtPlotRenderer` 做离屏导出，不必把主画布换掉。
- Qt Charts 只用于轻量仪表盘；不要在飞控回放页使用。
- JKQtPlotter 完整版不建议用于实时遥测；如果要继续评估，只考虑 `JKQTFastPlotter`，并补 100 万点同口径测试。
- 后续优化重点：自绘的 100 万点全量重绘仍为 34.5 ms/帧，下一步应做时间窗内数据索引/缓存，避免每次从全量数据重扫。

## 复现

本基准为一次性选型测试，基准代码未随仓库保留。核心做法可从本文口径表复现：
Qt 6.2 + offscreen 软件栅格、1280×720 视口、10 条曲线 × 10 万/100 万点、预热 2 帧后统计 5–15 帧中位数；自绘走 `ps_lod_minmax_many` 每像素列 min/max 桶后 `drawPolyline`。现行实现的绘图性能验收以 `tests/` 下 fastplot 与 `ps_test_plot_io` 契约测试为准。
