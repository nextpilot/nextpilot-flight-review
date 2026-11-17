include(Rust)
ps_import_kernel(ps_kernel)

# 第三方库
add_subdirectory(3rdparty)

if(PS_BUILD_GUI)
  find_package(Qt6 QUIET COMPONENTS Widgets Network Svg)
  if(Qt6_FOUND)
    # 底座：宿主扩展层与宿主服务
    add_subdirectory(application/runtime)
    add_subdirectory(application/modules/datasource/services/cache)

    # 停靠窗口：ADS 共享库（LGPL-2.1 vendor）
    add_subdirectory(3rdparty/ads)

    # 通用机械层：loader 公共代码与 CSV/JSON/binary
    if(PS_BUILTIN_COMMON AND PS_MODULE_DATASOURCE)
      add_subdirectory(extensions/builtin/common)
    endif()

    add_subdirectory(application/modules/datasource/services/live)

    # 内置扩展：业务组件在 extensions/builtin 聚合，base 只链接聚合 target
    add_subdirectory(extensions/builtin)

    # 底座 UI 与入口
    add_subdirectory(application)

    # 组件运行期资源部署：业务数据不写进 base。
    if(TARGET ps_gnss AND TARGET plotscope_app)
      add_custom_target(ps_deploy_gnss_db ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${PROJECT_SOURCE_DIR}/src/extensions/builtin/gnss/oem7_database.json
                $<TARGET_FILE_DIR:plotscope_app>/oem7_database.json
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${PROJECT_SOURCE_DIR}/src/extensions/builtin/gnss/nmea_database.json
                $<TARGET_FILE_DIR:plotscope_app>/nmea_database.json
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${PROJECT_SOURCE_DIR}/src/extensions/builtin/gnss/ubx_database.json
                $<TARGET_FILE_DIR:plotscope_app>/ubx_database.json
        DEPENDS plotscope_app)
    endif()

    if(TARGET ps_mavlink AND TARGET plotscope_app)
      add_custom_target(ps_deploy_mavlink_metadata ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${PROJECT_SOURCE_DIR}/src/3rdparty/mavlink/generated/mavlink_metadata.json
                $<TARGET_FILE_DIR:plotscope_app>/mavlink_metadata.json
        DEPENDS plotscope_app)
    endif()
  else()
    message(WARNING "未找到 Qt6 Widgets，只构建内核（-DPS_BUILD_GUI=OFF 可静默）")
  endif()
endif()
