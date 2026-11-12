-- 通用滑动平均，窗口由参数面板传进来
local win = ps.param("window", 5)
ps.plot("derived.smooth", ps.movingAvg(ps.ch(ps.param("channel", "att.pitch")), win))
