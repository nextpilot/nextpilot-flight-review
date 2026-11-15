# SARibbon spike（Ribbon 结构层可行性验证）

2026-11-12，SARibbon 2.9.6（HEAD `9ef57cd`，MIT），Qt 6.2.4，静态库。

## 结论（详见架构文档 §12「Ribbon / Dock spike」）

| 项 | 实测 |
|---|---|
| 七页签 + 分组 + 大/小按钮 | 成立（`addCategoryPage` / `addPanel` / `addLargeAction` / `addSmallAction`） |
| 右侧 qchip 位 | 成立（`ribbon->rightButtonGroup()->addAction`） |
| 展开高度（CompactTwoRow、隐藏标题栏、tab 26 / panel 15 / 图标 32） | **97 px**（原型 74 px） |
| 折叠高度（`setMinimumMode(true)`） | **28 px**（原型 32 px） |
| 暗色主题 | 静态库必须 `Q_INIT_RESOURCE(SARibbonResource)`，否则静默回退浅色 QSS |
| 观感 | Office 风格；贴原型需整体重写 QSS + 自绘 SVG 图标 |

## 跑法

```bash
git clone --depth 1 https://github.com/czyt1988/SARibbon /tmp/saribbon
cmake -S /tmp/saribbon -B /tmp/saribbon/build -DCMAKE_BUILD_TYPE=Release \
  -DSARIBBON_BUILD_EXAMPLES=OFF -DSARIBBON_BUILD_STATIC_LIBS=ON
cmake --build /tmp/saribbon/build -j4 && cmake --install /tmp/saribbon/build --prefix /tmp/saribbon-prefix

(cd src/kernel && cargo build --release -p ps-abi)
cmake -S tool/spikes/saribbon -B build/sari-spike -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/tmp/saribbon-prefix
cmake --build build/sari-spike -j4
xvfb-run -a -s "-screen 0 1600x1000x24" ./build/sari-spike/ps_sari_spike test/fixture/demo_flight.csv          # 展开
xvfb-run -a -s "-screen 0 1600x1000x24" ./build/sari-spike/ps_sari_spike test/fixture/demo_flight.csv min      # 折叠
```
