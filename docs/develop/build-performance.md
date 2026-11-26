# 构建性能：ccache / PCH / Unity build 实测

本文件记录 `[I4]` 的实验口径与实测数据，用于回答"几个加速手段能不能叠加、
叠加后是不是负收益"。**结论未稳定前，产品默认保持 `PS_USE_PCH=OFF`。**

## 1. 结论速览

| 组合                            | 冷构建    | 热全量          | 单文件增量 | 判定                     |
| ------------------------------- | --------- | --------------- | ---------- | ------------------------ |
| 仅 ccache（默认现状）           | 461 s     | 103 s           | 20 s       | 基准                     |
| 仅 PCH                          | **141 s** | 141 s（无缓存） | 8 s        | 冷编最优，热编全废       |
| PCH + ccache（默认 sloppiness） | 148 s     | 136 s           | —          | **净负收益**             |
| PCH + ccache + `sloppiness`     | 179 s     | 103 s           | 5 s        | 可用，但热路径无增量收益 |

三条硬结论：

1. **PCH 单独对冷编有效，约 3.27 倍**（461 s → 141 s）。
2. **PCH 与 ccache 默认同时开是净负收益**：ccache 把全部编译判为不可缓存，
   实测 `Hits 0/0`、`Misses 0`，两轮都接近全量重编——PCH 的 123 MB 代价白付，
   ccache 的日常收益全丢。
3. 加上 `CCACHE_SLOPPINESS=pch_defines,time_macros` 后 ccache 恢复（命中 95.45%），
   但**热全量与"仅 ccache"完全打平（103 s）**：命中路径上 ccache 直接拿现成
   `.o`，根本不调用编译器，PCH 在这里零收益。
4. **生成器换成 Ninja 是当前性价比最高的手段**：Unix Makefiles 每次构建先付一笔
   解析税（`make help` 单独实测 4.71–5.68 s），Ninja 为 55–76 ms；ccache 在 Ninja
   下增量命中实测 100%。详见 §9。

## 2. 测试口径

- 样本：`SARibbonBar` target（第三方 vendor 库，45 个编译单元，体量适中、一轮 2 分钟量级）
- 机器：2 核 3 GB 内存，Linux，GCC 11，ccache 4.5.1，CMake 4.x + Makefile 生成器
- 每个组合**独立 build 目录 + 独立 `CCACHE_DIR`**，避免相互污染
- 每场景前 `ccache -z` 清统计；单文件增量场景 3 轮取中位，每轮写入不同内容（避免命中假象）
- PCH 头文件：`cmake/pch.hpp`（Qt 常用核心类型，不放项目自身头文件）

## 3. 关键机制：PCH 会旁路 ccache

开 PCH 后编译命令行多出 `-include <build>/cmake_pch.hxx`。ccache 默认把这类
调用判为 **Uncacheable**，结果是：

- `ccache -s` 里 `Hits: 0 / 0`、`Misses: 0`——一个可缓存调用都没有；
- 第二轮回合同样接近全量重编（136 s vs 冷编 148 s）；
- 缓存目录几乎不增长（2.2 MB，仅少量非 PCH 调用进入）。

放行 `pch_defines`（PCH 定义）与 `time_macros`（`__TIME__` 这类宏）之后，
ccache 才重新开始记录与命中。

## 4. 放行值怎么传（两个坑，都已实测）

1. **`set(ENV{...})` 无效**。它只作用于 configure 进程；编译时 ccache 是
   构建工具（make/ninja）的子进程，继承不到，等于没设。
2. **launcher 包装也没复现成功**。`cmake -E env CCACHE_SLOPPINESS=... ccache`
   确实能把变量传到 ccache 进程（已单独验证 env 传递本身正常），但该路径下
   ccache 仍判 Uncacheable，未能复现 95% 命中。

因此 `cmake/Pch.cmake` **不做自动注入**，改为配置期硬检查：开启 PCH 且检测到
ccache 时，若 `CCACHE_SLOPPINESS` 不含 `pch_defines` 就 `FATAL_ERROR`，并给出
两条出路（export 放行，或 `-DPS_USE_CCACHE=OFF`），避免"以为开了、实际没开"的静默失效。

已验证生效的用法：

```bash
export CCACHE_SLOPPINESS=pch_defines,time_macros
cmake -S . -B build -DPS_USE_PCH=ON -DPS_PCH_TARGETS=SARibbonBar
cmake --build build -j"$(nproc)"
```

## 5. 使用建议

- **日常开发（改改再编）**：只留 ccache，别开 PCH。热路径全靠 ccache 命中，
  增量 20 s 里剩余部分是链接与工具启动，PCH 帮不上忙。
- **CI 首次构建 / 新机器零缓存**：PCH 收益最大（3.27 倍），此时 ccache 本来就
  全 miss，可以 `-DPS_USE_CCACHE=OFF -DPS_USE_PCH=ON` 换冷编速度。
- **两个都要**：必须显式 `export CCACHE_SLOPPINESS=pch_defines,time_macros`，
  并接受 PCH 的磁盘与内存代价，见下节。
- **Unity build**：尚未实测，见 §8。

## 6. ccache 的三处配置（已落地）

三项都针对"缓存明明开着、却在换场景时全 miss"，彼此不是可选优化，缺一项就漏一段收益。

| 配置                 | 位置                                | 解决什么                                                               |
| -------------------- | ----------------------------------- | ---------------------------------------------------------------------- |
| `hash_dir = false`   | `$CCACHE_DIR/ccache.conf`           | `build/` 与 `build-debug/` 共用缓存，切 preset 不再从零编              |
| `depend_mode = true` | 同上                                | 用 `-MD` 的依赖信息判命中，跳过完整预处理；命中判断更准也更快          |
| `CCACHE_BASEDIR`     | `CMakePresets.json` → `environment` | 换 checkout 目录后仍命中；由 `${sourceDir}` 动态展开，换机器不用改配置 |
| `-ffile-prefix-map`  | 根 `CMakeLists.txt` 全局            | 抹平 `-I`/`-o`/`__FILE__`/DWARF 里的绝对前缀，`BASEDIR` 之外的那部分   |

为什么这样分层：与路径无关的策略写进 `ccache.conf`，`$CCACHE_DIR` 指到哪它就跟到哪，CI 与 IDE 都认；与仓库绝对路径绑定的 `base_dir` 只能交给 `CMakePresets.json`，用 `${sourceDir}` 展开；而编译期真正写进产物的路径，必须靠编译选项抹平——`BASEDIR` 管不到这一层。

注意两点：

- `-ffile-prefix-map` 会让 `__FILE__` 变成相对路径。如果有代码或测试断言了源码绝对路径，会在这里暴露。
- Release（`-O2`）与 Debug（`-g`）是两套不同的哈希，路径前缀一致也不代表能互相命中。两个 preset 要各暖各的。

## 7. PCH 的代价（不能只看加速）

- **gch 体积 123 MB / target**（本样本）。多个 target 各开一份，磁盘占用线性增长。
- **内存**：每个并发编译进程都要映射 gch，2 核 3 GB 机器上 `-j2` 已接近上限；
  内存更紧时可能触发 OOM 或剧烈换页，实测第二轮耗时波动（63–151 s）与此有关。
- **PCH 失效即全量重编**：`cmake/pch.hpp` 或任一被它包含的头变动，所有依赖该
  PCH 的 TU 一起重编。头文件越"公共"，这个风险越大——所以 pch.hpp 只放稳定
  的 Qt 核心类型，不放项目头。
- **ccache 条目变大**：C2 组缓存 24 MB / 44 单元，约为无 PCH 时的 8 倍。

## 8. 未完成项

- **Unity build 与 AUTOMOC 的交互**尚未实测（`PS_USE_UNITY` / `PS_UNITY_SIZE`
  开关未实现）。预期风险：moc 生成文件的包含关系被合并后，增量粒度变粗，
  与 ccache 命中粒度冲突。
- **全量工程矩阵**（264 个 `.cpp`、84 份 `mocs_compilation.cpp`、`App.cpp` 被
  11 个测试 target 引用）未在本机跑完，冷构成本机需 80 分钟量级；建议交给
  5 核 8 GB 机器，那里已有基线：冷 403 s、热全量 18.24 s / 稳态 17.79 s（22.6 倍）。
- **`App.cpp` 重复编译已解决**：原先被 11 个测试 target 各自 `target_sources`
  引入，现统一链 `ps_app`（PR #78）。`PS_APP_OBJECT` 方案因此不再需要。
- Unity build 仍不评估：91 处 `Q_OBJECT` 意味着大量 moc 产物，合并 TU 后与
  AUTOMOC 的包含关系、匿名命名空间与宏冲突风险都很高。

## 9. 生成器：Unix Makefiles → Ninja

口径：2 核 3 GB，GCC 11.4，CMake 4.4.4，ccache 4.5.1，Release，143 个编译单元。

### 9.1 收益从哪来

Make 是递归生成：每个子目录一份 Makefile，跨目录目标要逐层解析。
**这笔开销与"编不编东西"无关**，是每次构建的固定税：

| 项                                                 | Unix Makefiles |               Ninja |
| -------------------------------------------------- | -------------: | ------------------: |
| 构建图解析（`make help` / `ninja -t targets all`） | 4713 / 5678 ms | **55 / 58 / 76 ms** |
| `ps_command_autogen`（0 moc 汇总）                 |         800 ms |      **108–124 ms** |
| `ps_host_autogen`（1 moc）                         |        4752 ms |      **467–476 ms** |

autogen 那 4.75 s 的主体不是 moc，而是递归解析：`make -n ps_host_autogen`
单独测 1.50–1.74 s，`make help` 4.71–5.68 s。

### 9.2 ccache 仍有效（实测，不是推断）

- **官方支持**：CMake `<LANG>_COMPILER_LAUNCHER` 文档写明 Makefile 与 Ninja 生成器
  都会把编译器及其参数交给 launcher，示例工具即 ccache。
- **单文件增量**：不 reconfigure，`touch src/libs/fastplot/src/Axis.cpp` 后
  `cmake --build --preset default --target fastplot` → `1 hit / 1 = 100%`，连续两轮稳定。
- **autogen 汇总 TU**：删除 `mocs_compilation.cpp.o` 重编 → `1 hit / 1 = 100%`，
  连续两轮稳定（`mocs_compilation.cpp` md5 不变）。
- **多文件**：`touch` 3 个源文件 → `3 hit / 3 = 100%`，4.17 s 完成。

### 9.3 一次性成本

- **旧缓存不复用**：ccache direct mode 把 `-I` / `-include` / `-D` 计入哈希，
  `build/` 与 `build-ninja/` 的 autogen 路径不同，实测 28 个单元只命中 1 个；
  即使目录同名，`-MT` / `-MF` 等生成器相关参数仍有差异。
- 但**换生成器本来就必须重建构建树**（CMake 拒绝在生成器不匹配的目录上 configure），
  所以这次全量重编不构成额外代价。
- **每次重新 configure 后当轮会 miss 一次**，之后恢复。日常是"configure 一次 +
  反复 build"，不受影响。

### 9.4 迁移步骤

```bash
rm -rf build build-debug         # 生成器不匹配，必须重建
cmake --preset default           # 现在落到 Ninja
cmake --build --preset default -j"$(nproc)"
```

`tools/ubuntu-setup.sh --warm-cache` 不用改：它通过 preset 驱动 configure + build，
生成器随 preset 走。
