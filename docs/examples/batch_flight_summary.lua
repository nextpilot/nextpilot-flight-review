-- 无人值守用例：逐架次读入 → 取 roll → 统计与频谱 → 汇总表 → 关闭源
-- 跑法：plotscope_app --run docs/examples/batch_flight_summary.lua
-- 数据：python3 tools/gen-seed-flights.py /tmp/ps_seed
local dir = "/tmp/ps_seed/"
print("flight,points,rms,min,max,peak_freq_hz,peak_mag")
for i = 1, 5 do
    local name = string.format("flight_%02d.csv", i)
    local job = ps.app.load(dir .. name)
    job:wait()
    -- load 后新源排在末尾；取最后一个，避免依赖全局 id
    local src = ps.app.sources()[#ps.app.sources()]
    local ch = src:channel("roll")      -- 通道句柄直接进计算库：零拷贝 + 自动采样率
    local st = ps.stats(ch)
    local f = ps.fft(ch)
    local mi, mv = 1, -1
    for k = 2, #f.mag do
        if f.mag[k] > mv then mv, mi = f.mag[k], k end
    end
    print(string.format("%s,%d,%.3f,%.3f,%.3f,%.3f,%.3f",
        name, st.count, st.rms, st.min, st.max, f.f[mi], mv))
    ps.app.close(src.id)
end
print("remaining:", #ps.app.sources())
