# canvas_spike —— 画布选型基准（M0-0）

对比三条路，同一批数据、同一视口、同一台机器：

1. `CanvasWidget`（自绘）：内核 Rust `ps_lod_minmax` 抽到 1500 桶，UI 侧 `drawPolyline`；
2. `JKQTPlotter`：原始点全量交给库（16 万点/帧）；
3. `JKQTPlotter`：只喂抽稀后的 1500 点/通道。

数据是 20 通道 × 8000 点（100 Hz / 80 s，正弦 + 噪声），视口 1500×900，预热 3 帧后取 30 帧均值。

## 实测（2026-11-12）

环境：Qt 6.2.4、4 vCPU 云主机、软件光栅、`QT_QPA_PLATFORM=offscreen`、`QWidget::render`；
JKQTPlotter 5.0.0（HEAD `9e9f0af`，静态库）。

| 方案 | 单帧 |
|---|---|
| 自绘 + LOD，AA 关、线宽 1.0 | **7.2 ms** |
| 自绘 + LOD，AA 开、`QPainterPath`、线宽 1.3 | 915 ms |
| JKQTPlotter 原始 16 万点/帧（AA 开关无差别） | 60–64 ms |
| JKQTPlotter + 同样的 LOD（20×1500） | 13.1 ms |

同批 20 条 × 1500 段折线的 Qt 光栅基线：AA 开 367 ms；AA 关、线宽 1.0 **3.6 ms**；
AA 关但线宽 1.3 26.5 ms（小数线宽会退化成宽线轮廓器）。结论见架构文档 §11 铁律 4 与 §12「M0-0 结论」。

## 跑法

```bash
# 1) 编译并安装 JKQTPlotter 到一个 prefix（上游要求 CMake ≥ 3.23、Qt6 Core/Gui/Widgets/Svg/PrintSupport/Xml/OpenGL）
git clone --depth 1 https://github.com/jkriege2/JKQtPlotter /tmp/jkqt
cmake -S /tmp/jkqt -B /tmp/jkqt/build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF
cmake --build /tmp/jkqt/build -j4
cmake --install /tmp/jkqt/build --prefix /tmp/jkqt-prefix

# 2) 内核先出 release 静态库
(cd src/kernel && cargo build --release -p ps-abi)

# 3) 跑基准
cmake -S test/perf/canvas_spike -B build/bench -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/tmp/jkqt-prefix
cmake --build build/bench -j4
(cd build/bench && QT_QPA_PLATFORM=offscreen ./ps_canvas_spike)
```

基准只量"一帧画出来的时间"，不含事件循环、合成与 GPU 上屏；换机器要重跑。
