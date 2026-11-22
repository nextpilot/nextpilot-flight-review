# PlotScope 用户手册

本目录只写「装好之后怎么用」。编译、架构和扩展开发见 [`../develop/`](../develop/README.md)。

| 文档 | 内容 |
|---|---|
| [快速开始](getting-started.md) | 启动、打开日志、选通道、多源对比、工作区保存、命令行 |
| [支持的数据格式](formats.md) | 各格式能读到什么、时间轴怎么处理、实时源与录制、已知限制 |
| [绘图与交互](plotting.md) | 图型、画布切分、缩放、游标、属性面板、导出 |
| [变换、脚本与设置](operations.md) | 数据变换、Lua 脚本工作台、设置、快捷键、排错 |

## 建议阅读顺序

1. 第一次使用：[快速开始](getting-started.md)。
2. 不确定文件能不能打开：[支持的数据格式](formats.md)，或直接 `plotscope --list-parser`。
3. 想把分析自动化：[变换、脚本与设置](operations.md) 的脚本工作台与命令行。
