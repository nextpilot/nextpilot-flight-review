-- 批处理：把整份日志的多通道重采样到 100 Hz 栅格，落 NDF
local dst = ps.grid(100)
for _, name in ipairs(ps.channels()) do
  ps.write(name, ps.resample(ps.ch(name), dst))
end
