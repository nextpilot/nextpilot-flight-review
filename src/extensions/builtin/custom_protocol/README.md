# 自定义协议组件

本目录是 app 内置的“顺序帧协议”组件，协议编辑器、协议结构校验、codec、dataloader 放在同一个边界内。

## 组成

- `ProtocolEditorDialog.*`：内置 Qt 编辑器，只编辑顺序段协议。
- `CustomProtocolComponent.*`：向 `ComponentRegistry` 注册协议、Ribbon 入口、动作和 loader。
- `SequentialCodec.*`：把协议 JSON 解释为帧通道。
- `CustomProtocolLoader.cpp`：文件 loader 与组件注册入口。

## 约束

- 协议 JSON 的结构知识只维护在 `src/protocol/ProtocolSpec` 与本组件的 model 中；编辑器和 codec 都调用同一套校验与类型表。
- 组件是 app 的基本功能，可以直接用 C++/Qt；外部扩展走 ABI，只注册 loader/protocol/schema，不跨 ABI 返回 `QWidget*`。
- Ribbon 入口由组件贡献，`MainWindow` 只负责按 contribution 渲染，不再硬编码协议页。
