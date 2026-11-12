# JKQTPlotter 上游没有官方 CMake 包，这里给出最小查找模块（D3 / D12）。
# 用法：-DPS_JKQT_ROOT=<jkqtplotter 源码或安装目录>；找不到时 GUI 退回自绘 Canvas。
find_path(JKQT_INCLUDE_DIR jkqtplotter.h
          HINTS ${PS_JKQT_ROOT} ${PS_JKQT_ROOT}/lib
          PATH_SUFFIXES lib jkqtplotter)
find_library(JKQT_LIBRARY NAMES jkqtplotter JKQTPlotter
             HINTS ${PS_JKQT_ROOT}/lib ${PS_JKQT_ROOT}/build)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(JKQTPlotter REQUIRED_VARS JKQT_INCLUDE_DIR JKQT_LIBRARY)

if(JKQTPlotter_FOUND AND NOT TARGET JKQTPlotter::JKQTPlotter)
  add_library(JKQTPlotter::JKQTPlotter UNKNOWN IMPORTED)
  set_target_properties(JKQTPlotter::JKQTPlotter PROPERTIES
    IMPORTED_LOCATION "${JKQT_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${JKQT_INCLUDE_DIR}")
endif()
