# Plot backend benchmark

同一个 Qt 进程里用同一组数据渲染 `PlottingCanvas`、QCustomPlot、Qwt，输出 JSON。

## 依赖

- Qt 6.2.4，与主工程一致
- QCustomPlot 2.1.1：解压到 `benchmarks/qcustomplot-source/`
- Qwt 6.3.0：源码放 `benchmarks/qwt-6.3.0/`，构建产物放 `benchmarks/qwt-build/`
- Qt Charts 可选；未安装时只跳过 `charts` backend

## 构建

```bash
cmake -S . -B build -DPS_BUILD_BENCHMARKS=ON
cmake --build build -j4 --target plot_bench
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
./build/benchmarks/plot_bench/plot_bench --lib self --curves 10 --points 100000 --frames 10
./build/benchmarks/plot_bench/plot_bench --lib qcustom --curves 10 --points 100000 --frames 10
./build/benchmarks/plot_bench/plot_bench --lib qwt --curves 10 --points 100000 --frames 10
```

`--zoom 0.01` 表示只看 1% 时间窗；`--adaptive 0` 关闭 QCustomPlot 自适应采样。
计时包含 backend 重绘和 `QWidget::render` 到 QImage，不只是 blit。
