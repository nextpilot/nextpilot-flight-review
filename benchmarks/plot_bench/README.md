# Plot backend benchmark

同一个 Qt 进程里用同一组数据渲染 `PlottingCanvas`、QCustomPlot、Qwt，输出 JSON。

## 依赖

- Qt 6.2.4，与主工程一致
- QCustomPlot 2.1.1：解压到 `benchmarks/qcustomplot-source/`
- Qwt 6.3.0：源码放 `benchmarks/qwt-6.3.0/`，构建产物放 `benchmarks/qwt-build/`
- JKQtPlotter 可选；安装到 `benchmarks/jkqt-install/` 后自动启用 `jkqt` backend
- Qt Charts 可选；未安装时只跳过 `charts` backend

## 构建

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DPS_BUILD_BENCHMARKS=ON
cmake --build build-release -j4 --target plot_bench
```

Qwt 构建示例：

```bash
cd benchmarks
mkdir -p qwt-build && cd qwt-build
../../deps/Qt/6.2.4/gcc_64/bin/qmake ../qwt-6.3.0/qwt.pro
make -j4 sub-src
```

## 运行

```bash
export QT_QPA_PLATFORM=offscreen
export LD_LIBRARY_PATH="$PWD/deps/Qt/6.2.4/gcc_64/lib:$PWD/benchmarks/qwt-build/lib"
./build-release/benchmarks/plot_bench/plot_bench --lib self --curves 10 --points 100000 --frames 10
./build-release/benchmarks/plot_bench/plot_bench --lib qcustom --curves 10 --points 100000 --frames 10
./build-release/benchmarks/plot_bench/plot_bench --lib qwt --curves 10 --points 100000 --frames 10
```

`--zoom 0.01` 表示只看 1% 时间窗；`--adaptive 0` 关闭 QCustomPlot 自适应采样；`--jkqtCompress 0` 关闭 JKQtPlotter 线压缩；`--predecimate 1` 让所有后端只接收 2×像素列 min/max 点。
计时包含 backend 重绘和 `QWidget::render` 到 QImage，不只是 blit。

JKQtPlotter 构建示例：

```bash
cd benchmarks
cmake -S jkqtplotter-source -B jkqt-build -DCMAKE_BUILD_TYPE=Release \
  -DJKQtPlotter_BUILD_EXAMPLES=OFF -DJKQtPlotter_BUILD_TESTS=OFF \
  -DJKQtPlotter_BUILD_TOOLS=OFF -DCMAKE_PREFIX_PATH="$PWD/../deps/Qt/6.2.4/gcc_64"
cmake --build jkqt-build -j4
cmake --install jkqt-build --prefix "$PWD/jkqt-install"
```

JKQtPlotter 的 `jkqt` backend 使用完整版 `JKQTPlotter`，不是精简的 `JKQTFastPlotter`。
