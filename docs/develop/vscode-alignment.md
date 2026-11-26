# VSCode 命名对位表

用途：设计文档、提交说明、代码注释里出现的**每个新名字**，都要能在这张表里指到出处；表里没有的，先补表再用。

口径：照 VSCode 原类名、原路径，不翻译、不加复数、不换同义词。表里 `⚠️` 的行表示路径/命名尚未核对，引用前必须用源码或官方文档核一次，核完改成 `✅` 或删掉。

## 1. 结构类（workbench 布局与控件）

| 本项目                    | VSCode 对位                                    | 出处（VSCode 仓 `src/` 下）                                                       | 状态               |
| ------------------------- | ---------------------------------------------- | --------------------------------------------------------------------------------- | ------------------ |
| `Part`                    | `Part`                                         | `vs/workbench/browser/part.ts`                                                    | ✅                 |
| `WorkbenchLayout`         | `Layout`（类名无前缀；本项目加前缀属仓内习惯） | `vs/workbench/browser/layout.ts`                                                  | ✅（前缀差异已知） |
| `EditorPart`              | `EditorPart`                                   | `vs/workbench/browser/parts/editor/editorPart.ts`                                 | ✅                 |
| `SidebarPart`             | `SidebarPart`                                  | `vs/workbench/browser/parts/sidebar/sidebarPart.ts`                               | ✅                 |
| `ActivitybarPart`         | `ActivitybarPart`                              | `vs/workbench/browser/parts/activitybar/activitybarPart.ts`                       | ✅                 |
| `StatusbarPart`           | `StatusbarPart`                                | `vs/workbench/browser/parts/statusbar/statusbarPart.ts`                           | ✅                 |
| `CompositeBar`            | `CompositeBar`                                 | `vs/workbench/browser/parts/compositeBar.ts`                                      | ✅                 |
| `ViewPane`                | `ViewPane`                                     | `vs/workbench/browser/parts/views/viewPane.ts`                                    | ✅                 |
| `PaneView`                | `PaneView`                                     | `vs/base/browser/ui/splitview/paneview.ts`（新版可能移到 `paneview/paneview.ts`） | ⚠️                 |
| `ViewContainerModel`      | `ViewContainerModel`                           | `vs/workbench/browser/parts/views/viewContainerModel.ts` 或 `services/views/…`    | ⚠️                 |
| `ActivityAction`          | `ActivityAction`                               | `vs/workbench/browser/parts/activitybar/activityAction.ts`                        | ⚠️                 |
| `NumberBadge`             | `NumberBadge`                                  | `vs/base/browser/ui/badge/badge.ts`                                               | ⚠️                 |
| `ActionBar`               | `ActionBar`                                    | `vs/base/browser/ui/actionbar/actionbar.ts`                                       | ✅                 |
| `MenuId` / `MenuRegistry` | 同名                                           | `vs/platform/actions/common/actions.ts`                                           | ✅                 |

## 2. 服务与契约

| 本项目                                                                      | VSCode 对位                                                                                                         | 出处                                                                                                                                | 状态                               |
| --------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------- |
| `IEditorService`                                                            | `IEditorService`                                                                                                    | `vs/workbench/services/editor/common/editorService.ts`                                                                              | ✅                                 |
| `IEditorResolverService`                                                    | 同名                                                                                                                | `vs/workbench/services/editor/common/editorResolverService.ts`                                                                      | ⚠️                                 |
| `IStatusbarService`                                                         | 同名                                                                                                                | `vs/workbench/services/statusbar/browser/statusbarService.ts`                                                                       | ⚠️（路径在 browser/common 间搬过） |
| `IViewDescriptorService`                                                    | 同名                                                                                                                | `vs/workbench/services/views/common/viewDescriptorService.ts`                                                                       | ⚠️                                 |
| `StatusBarItem` / `createStatusBarItem`                                     | `vscode.StatusBarItem` / `vscode.window.createStatusBarItem`                                                        | 《API 参考》StatusBarItem                                                                                                           | ✅                                 |
| `contributes.customEditors`                                                 | 同名                                                                                                                | 《扩展清单》`contributes.customEditors`（`viewType`/`displayName`/`selector` 均必填）                                               | ✅                                 |
| `CustomEditorProvider` / `registerCustomEditorProvider`                     | 同名                                                                                                                | 《API 参考》CustomEditorProvider                                                                                                    | ✅                                 |
| `contributes.configuration` / `ConfigurationSpec` / `ConfigurationProperty` | `contributes.configuration`（扁平 `properties` 点号键）                                                             | 《扩展清单》`contributes.configuration`                                                                                             | ✅                                 |
| `IConfigurationService`                                                     | `IConfigurationService`                                                                                             | `vs/platform/configuration/common/configuration.ts`                                                                                 | ✅                                 |
| `SettingsEditorWidget`（viewType `workbench.editor.settings2`）             | `SettingsEditor2`（editor pane ID = `'workbench.editor.settings2'`；input typeId 是 `'workbench.input.settings2'`） | `vs/workbench/contrib/preferences/browser/settingsEditor2.ts`；`vs/workbench/services/preferences/common/preferencesEditorInput.ts` | ✅                                 |
| `workbench.action.openSettings`                                             | 同名（内置命令）                                                                                                    | 《API 参考》commands / `vs/workbench/contrib/preferences/browser/preferencesContribution.ts`                                        | ✅                                 |
| `IDisposable` / `Disposable` / `toDisposable`                               | 同名                                                                                                                | `vs/base/common/lifecycle.ts`                                                                                                       | ✅                                 |

## 3. 无 VSCode 对位（仓内类型，注释里必须标注“无 VSCode 对位”）

| 本项目                                                                                      | 说明                                                                                                     |
| ------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------- |
| `ResourceRef`                                                                               | VSCode 用 `URI` + `IEditorInput`；本仓以单个 ref 代替，属**适配层**，不是照抄                            |
| `Subscription`                                                                              | 事件退订的仓内映射（`= QMetaObject::Connection`）；VSCode 的事件订阅同样返回 `Disposable`，映射差异见 §5 |
| `ToolboxHub` / `StatusHub` / `ContributionParser` / `PanelFactoryHub` / `DialogLauncherHub` | 数据层/装配层，VSCode 无对应类；`ToolboxHub`、`PanelFactoryHub`、`DialogLauncherHub` 随壳布局统一移除    |

## 4. 值域字符串

- 菜单 place：`menubar/<pageId>`（VSCode `MenuId.MenubarFile = 'menubar/File'` 同形），**不许写成点号**。
- 其余 place 走 VSCode 原值：`view/title`、`view/item/context`、`viewContainer/title`。
- `customEditors[].viewType` 全局唯一；重复 → 后到者拒载 + 诊断，且**非法条目不得占 id**（校验通过再 `claimId`）。

## 5. 撤销三件套（边界写死，不许互相替代）

| 场景                               | 类型                                                   | 谁负责                               |
| ---------------------------------- | ------------------------------------------------------ | ------------------------------------ |
| 运行层注册（provider、状态栏条目） | `IDisposable`（`std::unique_ptr`，`dispose()` 幂等）   | 注册者持有；owner 卸载时宿主代撤     |
| 事件退订                           | `workbench::Subscription`（`QMetaObject::Connection`） | `ISubscriptions::add` / `disposeAll` |
| owner 整批撤                       | `unregisterOwner(owner)`                               | registry 表自身                      |

禁止：三者互相替代；`IDisposable` 自带 owner 撤销（第二套聚合）；拿 `Subscription` 当注册句柄。

## 6. 参考项目与边界

原则：**借结构，不借代码，不借依赖。** 看别人的项目是为了确认某个部件的形状，不是把它的技术栈搬进来。

### 可借鉴（只取结构，采用前先核源码）

| 来源                          | 借什么                                                                                                 | 对应本仓                                                                                                                           |
| ----------------------------- | ------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------- |
| ADS `examples/centralwidget`  | 编辑区作为**永久中央区**，不走普通 dock 的关/浮逻辑                                                    | 中央区 `EditorPart`                                                                                                                |
| ADS Perspective 保存/恢复     | 布局二进制流 + 恢复顺序                                                                                | `Layout::saveState/applyState`（现由 `MainWindow::saveState` 与 `ShellBinder::applyState` 分散持有，应统一收进 `WorkbenchLayout`） |
| ADS `examples/dockindock`     | **只看不取**：嵌套分组属于“面板再嵌面板”，本仓视图层级只到 `sidebar / panel / auxiliary`，不引入第四层 | —                                                                                                                                  |
| 通用命令面板实现（QuickOpen） | 搜索框 + 模糊匹配 + 最近项                                                                             | `workbench.action.showCommands`（内建命令最小集）；**候选来源必须是 `commands` / `MenuRegistry` 贡献，壳不许自带命令表**           |

### 明确否决

| 项目/方案                                            | 否决理由                                                                                                                 |
| ---------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| Perseus IDE（C++23 + Qt6 + Qt WebEngine + xterm.js） | 技术栈方向不同：我们要绘图分析工作台，不是 IDE；WebEngine 与终端会显著抬高构建/打包成本（CI 三平台），且本项目无终端需求 |
| CodeAstra / CodeWizard（tree-sitter、LSP）           | 成熟度与规范不足；LSP/tree-sitter 不属于编辑器契约层，也不属于布局层                                                     |
| QCodeEditor                                          | Qt5 时代产物，Qt6 适配与维护成本高；高亮需求用 `QSyntaxHighlighter` 自写规则即可                                         |
| CodeMirror / Monaco / 任何嵌入式编辑器内核           | 浏览器栈与本项目 Qt Widgets + SARibbon + ADS 的 UI 栈冲突，体积与 IPC 成本不可接受                                       |

### 第三方依赖白名单

`src/3rdparty/` 现只有：`ads`（Qt Advanced Docking System 5.1.1，vendored）、`saribbon`、`lua-5.4`、`mavlink`。

- 新增任何 **UI 类**第三方库 = 先改本节白名单并取得评审同意，**不许在改动里顺手引入**；
- 数据/协议类库（如新的日志格式解析器）按模块边界单独评审，不进壳。

### 编辑器能力边界（写死）

- 现在：`BuiltinTextEditor` = `QPlainTextEdit` + `QFile`，只保证打开/编辑/保存；`canvas` 走 `customEditors` 的自定义编辑器类型。
- 之后：语法高亮（`QSyntaxHighlighter`，规则数据放扩展包）、折叠、补全、跳转属**独立能力**，不得混进编辑器契约层——契约层一旦长进编辑器功能，壳就会重新开始“懂业务”。

## 7. 维护规则

1. 新名字 = 表里加一行；加不出来就说明它不该被造。
2. `⚠️` 行使用前必须核一次，核完改 `✅` 或删掉——不许“大概是这个路径”就写进文档或代码。
3. 本表只覆盖本项目实际用到、需要写进设计与代码的名字，不追求覆盖全 VSCode。
