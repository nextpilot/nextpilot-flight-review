# ps_arrow：Arrow 导入导出通道（C Data / IPC）。
# 只依赖 nanoarrow（顶层 Dependencies 解析的 ps_nanoarrow），纯 C++17，
# 不碰 Qt / kernel / 上层业务类型；导入结果由调用方转 Dataset::appendFrame。
add_library(ps_arrow STATIC
    src/ArrowChannel.cpp
    include/ArrowChannel.h
)
target_include_directories(ps_arrow PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ps_arrow PUBLIC ps_nanoarrow)
set_target_properties(ps_arrow PROPERTIES CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
