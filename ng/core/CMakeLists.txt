# ng_core：B7 过渡聚合库（簇 8 删除）。
# core 目录已迁空，本 target 仅 PUBLIC 聚合 ng_context（其传递所有 L1 依赖），
# 使全仓既有 target_link_libraries(... ng_core ...) 与 include 路径零改动。
add_library(ng_core INTERFACE)
target_link_libraries(ng_core INTERFACE ng_context)
