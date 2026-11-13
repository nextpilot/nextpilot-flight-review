include(Rust)
ps_import_kernel(ps_kernel)

# 必须先处理 3dparty：app 链接 SARibbonBar
add_subdirectory(3rdparty)

if(PS_BUILD_GUI)
  find_package(Qt6 QUIET COMPONENTS Widgets Svg)
  if(Qt6_FOUND)
    add_subdirectory(plugin/csv)
    add_subdirectory(app)
  else()
    message(WARNING "未找到 Qt6 Widgets，只构建内核（-DPS_BUILD_GUI=OFF 可静默）")
  endif()
endif()
