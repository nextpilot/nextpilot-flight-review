-- ps.lua：所有算子脚本共用的宿主 API（由 ps-script 注入，非独立进程）
-- 约定：通道是 userdata/向量句柄，算术走元表 → 一次调用处理一整块（10 ms/块），禁止逐点循环。

-- ps.ch(name)        → 通道向量句柄（惰性，绑定到当前可视图层）
-- ps.derivative(v)   → 向量求导（Rust 侧）
-- ps.movingAvg(v,n)  → 滑动平均
-- ps.resample(v,t)   → 重采样到统一时间栅格
-- ps.plot(name, v)   → 生成派生通道并上画布
