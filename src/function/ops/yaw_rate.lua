-- 偏航角速度：整块向量算，一条脚本一次处理 10 ms 的数据
local roll = ps.ch("att.roll")
local yaw = ps.ch("att.yaw")
local yaw_rate = ps.movingAvg(ps.derivative(yaw), 5)
ps.plot("derived.yaw_rate", yaw_rate)
