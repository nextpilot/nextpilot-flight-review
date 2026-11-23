# AGENTS.md · libs/（L0-L1 机制与契约库）

> 目录归属权威见 [`../LAYERS.md`](../LAYERS.md)。本文件只保留本目录的硬约束。

## 本目录是什么

可复用的**机制与契约**库：foundation / dataset / registry / intent / parser / protocol / uischema / fastplot / context / module / recording。谁用谁实例化——库自身**没有进程语义**（不装载、不启动、不持有会话）。

## 允许放

- 纯接口与数据契约（无方法体、无状态；如 `libs/module` 的 IModule/IUiContext）
- 数据结构与算法、格式编解码、纯 metadata
- 无状态模板与工具（Result、OpDispatch）
- AppContext 运行态容器（仅 `libs/context`，唯一实例在 app 构造）

## 禁止放

- 全局活实例、单例、服务定位器、跨调用存活的可变对象
- 任何 `QWidget` / Qt Widgets 类型（**包括 libs/context**——headless 承诺）
- 业务逻辑、领域判断（"ULog 该怎么解析"这类知识归 toolbox）
- 进程装配/生命周期代码（那是 host/app 的事）
- 以"过渡暂存"为由新建杂项目录（core 已消亡，禁止重建）

## 依赖规则

- 只准依赖 CMake 里显式声明的更低层库；禁止 include `application/`（shell/gui/host/modules）与 `toolbox/` 下任何头
- 同层依赖必须单向且在 CMake 声明（如 dataset → foundation）；发现环 = 停下重构依赖，不许用前置声明糊弄过去

## 动手前自查

1. 新文件离开进程能独立编译、独立测试吗？不能 → 大概率不属于 libs
2. 它需要"知道"宿主或模块的存在吗？需要 → 违规
3. 头文件里出现实现体（非 inline/非模板）了吗？→ 移到 .cpp 或 recheck 归属
4. 新建子库前：先确认不能并进现有库；确需新建，同步更新 `LAYERS.md` L1 清单与 `libs/AGENTS.md`

## 守卫

configure 期 `cmake/LayerContractGuard.cmake` 扫描链接倒挂直接 FATAL_ERROR；运行期 `ps_test_architecture` 兜底。守卫只许加严，禁止加豁免。
