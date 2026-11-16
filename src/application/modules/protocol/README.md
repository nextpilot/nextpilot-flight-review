# 协议模块

自定义协议（`.psproto`）的编辑、校验与数据源接入。原 `extensions/builtin/custom_protocol` 已迁入 `modules/protocol`，协议不再算内置业务扩展。

## 组成

- `include/protocol/ProtocolModule.h`：模块入口 `ProtocolModule`，实现 `IComponent`，向 `Registry` 注册协议、Ribbon 入口、动作和 loader。
- `manager/ProtocolModule.cpp`：模块贡献与 `protocol.*` 动作（新建/打开/校验）。
- `manager/CustomProtocolLoader.cpp`：文件 loader，导出 `ps_custom_protocol_loader`。
- `dialog/ProtocolEditorDialog.*`：顺序段协议编辑器。
- `codec/SequentialCodec.*`：把协议 JSON 解释为帧通道。
- `protocol/ProtocolSpec.*`、`protocol/ProtocolDocument.*`：协议 schema 与文档模型（`ps_protocol` target）。

## 约束

- 协议 JSON 的结构知识只维护在 `protocol/ProtocolSpec` 与 `ProtocolDocument`，编辑器和 codec 共用同一套校验与类型表。
- 不 include `shell` 私有头，不持有 `MainWindow*`；窗口只经 `HostContext` 获取。
- Ribbon 入口由模块贡献，`MainWindow` 只按 contribution 渲染，不硬编码协议页。
- 外部扩展仍走 ABI，只注册 loader/protocol/schema，不跨 ABI 返回 `QWidget*`。
