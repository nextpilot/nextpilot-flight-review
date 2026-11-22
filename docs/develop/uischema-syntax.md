# uischema 语法规范 v1

一个 schema = 标题 + 字段数组。纯数据、可 JSON 往返、headless 可编译。字段分两类：数据字段（叶子，产生值）和布局字段（容器，装子字段）。渲染器递归遍历生成 UI。

字段名遵循五要素 `name / type / label / options / flags`，另有 `default` 等数据属性。type 命名直接对应 UI 控件，看到 type 就知道怎么展示。

## 1. 字段通用属性

| 属性 | 类型 | 适用 | 说明 |
|---|---|---|---|
| `name` | string | 全部 | 唯一标识；数据字段支持点路径 `time.mode`，收集时写成嵌套 JSON |
| `type` | string | 全部 | 字段类型，见 §2 |
| `label` | string | 全部 | UI 上显示的文字（如"文件名"）；布局字段作标题/标签名；缺省由 name 生成 |
| `options` | object | select | 候选值，见 §4 |
| `default` | value \| object | 数据字段 | 默认值或其来源，见 §5 |
| `flags` | string \| object | 全部 | 字段属性，见 §6 |
| `unit` | string | number | 单位，显示在控件右侧 |
| `description` | string | 全部 | 说明，作控件 tooltip |
| `fields` | array | 布局字段 | 子字段数组，递归，见 §3 |

数据属性（default/unit/description）在顶层，不进 flags——它们是值语义。

**解析约定（不区分大小写）**：语法关键字一律大小写不敏感，内部统一转小写比对——包括 `type` 值（`select`/`SELECT`/`Select` 等价）、`flags` 名、`from` 及其修饰键（`probeKey`/`labelKey`/`when`/`trim`/`set` 等）、特殊键 `from`。**数据值保持原样**：`name`、`label`、`default` 字符串、`options` 的 value 键与显示文本、点路径，均不改写大小写。

## 2. type 一览（type → UI）

### 数据字段（叶子，产生值）

| type | UI 控件 | 说明 |
|---|---|---|
| `string` | 单行文本框 `QLineEdit` | 文本 |
| `text` | 多行文本框 `QPlainTextEdit` | 长文本 |
| `number` | 数字框 `QSpinBox` / `QDoubleSpinBox` | 整数或浮点，由 default 值类型或 `precision` 决定 |
| `bool` | 勾选框 `QCheckBox` | 布尔 |
| `select` | 下拉框 `QComboBox` | 单选；显示 label，回传 value |
| `file` | 文本框 + 浏览按钮 | 文件路径，`flags.fileFilter` 定过滤器 |

### 布局字段（容器，不产生值）

| type | UI 容器 | 说明 |
|---|---|---|
| `tab` | `QTabWidget` | 每个 tab 是 `label` 命名的一页，`fields` 是该页内容 |
| `section` | `QGroupBox` | 带 `label` 标题的分组框，`fields` 是组内内容 |
| `horizontal` | `QHBoxLayout` | 子字段水平排成一行 |
| `vertical` | `QVBoxLayout` | 子字段垂直堆叠 |

布局字段用 `fields` 明确装哪些子字段，可任意嵌套（如 tab 里放 section，section 里放 horizontal）。不使用扁平标记项——水平/垂直布局必须知道边界。

## 3. 布局嵌套示例

```jsonc
{
  "name": "opts", "type": "tab", "label": "解析选项",
  "fields": [
    { "name": "basic", "type": "section", "label": "基本", "fields": [
        { "name": "delimiter", "type": "select", "label": "分隔符", "options": {...} },
        { "name": "has_header", "type": "bool", "label": "第一行是表头", "default": true }
    ]},
    { "name": "tfmt", "type": "horizontal", "fields": [
        { "name": "skip_rows", "type": "number", "label": "跳过开头行", "default": 0 },
        { "name": "time.mode", "type": "select", "label": "时间轴", "options": {...} }
    ]}
  ]
}
```

## 4. select 的 options

静态用对象字面量：`key` = 回传 value，`value` = 显示 label。

```jsonc
"options": {
  "row": "行号（不解析时间）",
  "column": "指定一列作为时间",
  "combined": "日期列 + 时间列合并"
}
// 下拉显示中文，写进结果的是 row / column / combined。
```

动态来源：候选来自 probe 结果，用特殊键 `from`。

```jsonc
"options": {
  "from": { "probeKey": "columns", "labelKey": "name",
            "filterKey": "kind", "filterKind": "all" }
}
// 生成 { 列下标: 列名 }：显示列名，回传列下标。filterKind="all" 不过滤。
```

`from` 修饰：`labelKey` 取显示名的键，`filterKey`/`filterKind` 按某键过滤候选。

## 5. default 默认值

静态直接写值；动态用 `{from:...}`。

```jsonc
"default": "auto"                                   // 静态
"default": { "from": { "probeKey": "delimiter", "trim": 1 } }
"default": { "from": { "probeKey": "suggested_time", "set": "column", "when": "ge:0" } }
```

`from` 修饰：`trim` 截断字符串；`when` 满足才生效（`ge:N` / `gt:N` / `exists`）；`set` 命中时改写成固定值。options 与 default 共用 `{from}` 语法，一处概念。

## 6. flags 字段属性

两种写法等价，可混用：

```jsonc
"flags": "readonly|nonNegative"                          // 开关型：管道分隔
"flags": { "readonly": true, "nonNegative": true, "min": 0 }  // 对象型：带参数
```

| flag | 参数 | 说明 |
|---|---|---|
| `required` | — | 必填 |
| `readonly` | — | 只读 |
| `hidden` | — | 恒隐藏 |
| `nonNegative` | — | 数值禁止负值 |
| `dependsOn` | `{name, equals}` | 依赖：name 字段回传值等于 equals 时才启用 |
| `min` / `max` | number | 数值范围 |
| `precision` | int | 小数位；缺省视为整数 |
| `fileFilter` | string | file 字段的文件过滤器 |
| `columns` | int | horizontal 的分栏数（缺省按子字段数均分） |

开关型（required/readonly/hidden/nonNegative）可写进管道串；带参数的只能写对象。

## 7. 渲染与回传约定

- 布局字段只切分结构，不产生控件值、不写进结果。
- `select` 回传 options 的 key（value）；显示 options 的值（label）。
- `dependsOn` 用被依赖字段的回传 value 匹配；不满足时控件禁用，该字段不计入结果。
- `name` 含点路径时收集成嵌套 JSON：`time.mode` → `{"time":{"mode":...}}`。
- probe 结果由 `probeFile()` 提供，作渲染期来源上下文；渲染器不读文件。
- 缺 label 时由 name 生成（下划线转空格、首字母大写）。

## 8. 实现补充约定

- `default` 静态值与动态来源可共存：静态 `default` 兜底，独立 `defaultFrom: {from:...}` 命中才覆盖（探测预填语义）。也接受 `default: {from:...}` 写法。
- `dependsOn` 未满足时控件禁用并隐藏，收集结果时跳过。
- 旧 snake_case 键（`source`/`initial_from_probe`/`visible_if`/`filter_kind`）在解析层仍兼容，新 schema 一律用本语法。
