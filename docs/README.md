# PlotScope 文档

文档按用途分目录，和代码同仓维护。

| 目录 | 放什么 | 读者 |
|---|---|---|
| [`manual/`](manual/README.md) | 安装后的使用说明、格式、绘图、变换、脚本与设置 | 用户 |
| [`develop/`](develop/README.md) | 已成熟的开发指南、当前架构、扩展模型、协议规范 | 开发者 |
| [`screenshots/`](screenshots/) | 界面截图，按功能或里程碑命名 | 所有人 |
| [`prototype/`](prototype/) | 可评审的单文件 UI 原型 | 所有人 |

## 查看原型

直接用浏览器打开 [`prototype/PlotScope-工作区原型.html`](prototype/PlotScope-工作区原型.html)。单文件自包含，不依赖本地服务、不联网。

## 约定

- 仓库级架构边界见根目录 [`../AGENTS.md`](../AGENTS.md)。
- 已实现的能力写进 `manual/`，架构机制写进 `develop/` 或 `ng/ARCHITECTURE.md`。
- 架构的使用方式和扩展接口说明写进 `develop/`；被否决的方案随删除清理，不留过程稿。
- 文档与原型不进安装包；安装包里的 `docs/` 只放许可与第三方声明。
