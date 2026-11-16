# ScriptEngine

Lua 5.4 脚本执行器。脚本只负责编排：输入是当前画布已有的时间序列，输出是新的派生序列。

```lua
rate = ps.derivative(ps.ch("att.roll"))
out = ps.movingAvg(rate, 5)
ps.plot("roll_rate", out)
```

## 沙箱

- 去掉 `os`、`io`、`package`、`debug`、`dofile`、`loadfile`、`require`。
- 20M 条指令上限，64 MB 分配上限；超限返回错误，不崩 UI。
- 不提供文件、网络、进程访问。

## 边界

- 输出序列长度以当前画布第一条曲线的时间轴为准；长度不同时截断或按索引补时间。
- 脚本结果只进当前画布，暂不写入 `.psl` 的派生配方。
- 本扩展当前只注册脚本执行器（`addScriptRunner`）：脚本面板、脚本 Ribbon 已从绘图页移除，待独立脚本模块提供，届时由该模块负责脚本 UI 与全局自动化编排。
