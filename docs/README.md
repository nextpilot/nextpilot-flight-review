# PlotScope 文档

文档按用途分目录，和代码同仓维护。

| 目录 | 放什么 | 读者 |
|---|---|---|
| [`manual/`](manual/README.md) | 安装后的使用说明、格式、绘图、变换、脚本与设置 | 用户 |
| [`develop/`](develop/README.md) | 架构、编译、开发约定、测试、扩展模型、协议规范 | 开发者 |
| [`roadmap/`](roadmap/README.md) | 后续计划、方案讨论、未实施的能力 | 开发者 / 评审 |
| [`screenshots/`](screenshots/) | 界面截图，按功能或里程碑命名 | 所有人 |
| [`prototype/`](prototype/) | 可评审的单文件 UI 原型 | 所有人 |

## 查看原型

直接用浏览器打开 [`prototype/PlotScope-工作区原型.html`](prototype/PlotScope-工作区原型.html)。单文件自包含，不依赖本地服务、不联网。

## 约定

- 已实现的能力写进 `manual/`，不要写进 `roadmap/`。
- 规划中、未定稿、需要讨论的方案写进 `roadmap/`，开头标明状态。
- 架构决策和扩展接口写进 `develop/`；过程稿不保留多份副本。
- 文档与原型不进安装包；安装包里的 `docs/` 只放许可与第三方声明。
