# VSCode 命名对位表

用途：卡面、PR 正文、代码注释里出现的**每个新名字**，都要能在这张表里指到出处；表里没有的，**先补表再落码**。

口径：照 VSCode 原类名、原路径，不翻译、不加复数、不换同义词。表里 `⚠️` 的行说明路径/命名我没十足把握，落码前必须用源码或官方文档核一次，核完改成 `✅` 或删掉。

## 1. 结构类（workbench 布局与控件）

| 本项目 | VSCode 对位 | 出处（VSCode 仓 `src/` 下） | 状态 |
|---|---|---|---|
| `Part` | `Part` | `vs/workbench/browser/part.ts` | ✅ |
| `WorkbenchLayout` | `Layout`（类名无前缀；本项目加前缀属仓内习惯） | `vs/workbench/browser/layout.ts` | ✅（前缀差异已知） |
| `EditorPart` | `EditorPart` | `vs/workbench/browser/parts/editor/editorPart.ts` | ✅ |
| `SidebarPart` | `SidebarPart` | `vs/workbench/browser/parts/sidebar/sidebarPart.ts` | ✅ |
| `ActivitybarPart` | `ActivitybarPart` | `vs/workbench/browser/parts/activitybar/activitybarPart.ts` | ✅ |
| `StatusbarPart` | `StatusbarPart` | `vs/workbench/browser/parts/statusbar/statusbarPart.ts` | ✅ |
| `CompositeBar` | `CompositeBar` | `vs/workbench/browser/parts/compositeBar.ts` | ✅ |
| `ViewPane` | `ViewPane` | `vs/workbench/browser/parts/views/viewPane.ts` | ✅ |
| `PaneView` | `PaneView` | `vs/base/browser/ui/splitview/paneview.ts`（新版可能移到 `paneview/paneview.ts`） | ⚠️ |
| `ViewContainerModel` | `ViewContainerModel` | `vs/workbench/browser/parts/views/viewContainerModel.ts` 或 `services/views/…` | ⚠️ |
| `ActivityAction` | `ActivityAction` | `vs/workbench/browser/parts/activitybar/activityAction.ts` | ⚠️ |
| `NumberBadge` | `NumberBadge` | `vs/base/browser/ui/badge/badge.ts` | ⚠️ |
| `ActionBar` | `ActionBar` | `vs/base/browser/ui/actionbar/actionbar.ts` | ✅ |
| `MenuId` / `MenuRegistry` | 同名 | `vs/platform/actions/common/actions.ts` | ✅ |

## 2. 服务与契约

| 本项目 | VSCode 对位 | 出处 | 状态 |
|---|---|---|---|
| `IEditorService` | `IEditorService` | `vs/workbench/services/editor/common/editorService.ts` | ✅ |
| `IEditorResolverService` | 同名 | `vs/workbench/services/editor/common/editorResolverService.ts` | ⚠️ |
| `IStatusbarService` | 同名 | `vs/workbench/services/statusbar/browser/statusbarService.ts` | ⚠️（路径在 browser/common 间搬过） |
| `IViewDescriptorService` | 同名 | `vs/workbench/services/views/common/viewDescriptorService.ts` | ⚠️ |
| `StatusBarItem` / `createStatusBarItem` | `vscode.StatusBarItem` / `vscode.window.createStatusBarItem` | 《API 参考》StatusBarItem | ✅ |
| `contributes.customEditors` | 同名 | 《扩展清单》`contributes.customEditors`（`viewType`/`displayName`/`selector` 均必填） | ✅ |
| `CustomEditorProvider` / `registerCustomEditorProvider` | 同名 | 《API 参考》CustomEditorProvider | ✅ |
| `IDisposable` / `Disposable` / `toDisposable` | 同名 | `vs/base/common/lifecycle.ts` | ✅ |

## 3. 无 VSCode 对位（仓内类型，注释里必须标注“无 VSCode 对位”）

| 本项目 | 说明 |
|---|---|
| `ResourceRef` | VSCode 用 `URI` + `IEditorInput`；本仓以单个 ref 代替，属**适配层**，不是照抄 |
| `Subscription` | 事件退订的仓内映射（`= QMetaObject::Connection`）；VSCode 的事件订阅同样返回 `Disposable`，映射差异见 §5 |
| `ToolboxHub` / `StatusHub` / `ContributionParser` / `PanelFactoryHub` / `DialogLauncherHub` | 数据层/装配层，VSCode 无对应类；`ToolboxHub`、`PanelFactoryHub`、`DialogLauncherHub` 在 A2b 起退役 |

## 4. 值域字符串

- 菜单 place：`menubar/<pageId>`（VSCode `MenuId.MenubarFile = 'menubar/File'` 同形），**不许写成点号**。
- 其余 place 走 VSCode 原值：`view/title`、`view/item/context`、`viewContainer/title`。
- `customEditors[].viewType` 全局唯一；重复 → 后到者拒载 + 诊断，且**非法条目不得占 id**（校验通过再 `claimId`）。

## 5. 撤销三件套（边界写死，不许互相替代）

| 场景 | 类型 | 谁负责 |
|---|---|---|
| 运行层注册（provider、状态栏条目） | `IDisposable`（`std::unique_ptr`，`dispose()` 幂等） | 注册者持有；owner 卸载时宿主代撤 |
| 事件退订 | `workbench::Subscription`（`QMetaObject::Connection`） | `ISubscriptions::add` / `disposeAll` |
| owner 整批撤 | `unregisterOwner(owner)` | registry 表自身 |

禁止：三者互相替代；`IDisposable` 自带 owner 撤销（第二套聚合）；拿 `Subscription` 当注册句柄。

## 6. 维护规则

1. 新名字 = 表里加一行；加不出来就说明它不该被造。
2. `⚠️` 行落码前必须核一次，核完改 `✅` 或删掉——不许“大概是这个路径”就写进卡面。
3. 本表只覆盖本项目实际用到、且要写进卡面的名字，不追求覆盖全 VSCode。
