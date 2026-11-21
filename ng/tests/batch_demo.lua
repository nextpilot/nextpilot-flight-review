-- M2.4 批量处理演示脚本：每个文件计算关键指标并 return（进汇总表）
-- batch.file / batch.dataset 由批量执行器注入（modules/script/BatchRunner.cpp）。
-- 取数走 data.series 意图（与 GUI/CLI/MCP 同一条读数出口）。

local id = batch.dataset
local name = batch.file:match("([^/\\]+)$")

local ok, r = pcall(function()
  return app.call("data.series", { dataset = id, column = "engine_rpm[rpm]" })
end)
if not ok or type(r) ~= "table" then
  return { file = name, note = "engine_rpm 列不可用" }
end

local v = r.v or {}
local sum, count = 0, 0
local mn, mx
for _, x in ipairs(v) do
  sum = sum + x
  count = count + 1
  mn = mn and math.min(mn, x) or x
  mx = mx and math.max(mx, x) or x
end

return {
  file = name,
  samples = count,
  rpm_mean = count > 0 and math.floor(sum / count * 10) / 10 or 0,
  rpm_min = mn,
  rpm_max = mx,
}
