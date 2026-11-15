include(Rust)
ps_import_kernel(ps_kernel)

# 第三方库
add_subdirectory(3rdparty)

if(PS_BUILD_GUI)
  find_package(Qt6 QUIET COMPONENTS Widgets Network Svg)
  if(Qt6_FOUND)
    # 底座：宿主扩展层与宿主服务
    add_subdirectory(base/host)
    add_subdirectory(base/host/services/cache)

    # 动态扩展公共设施
    add_subdirectory(extensions/dynamic/data_loader/common)

    add_subdirectory(base/host/services/live)

    # 动态扩展：内置数据加载器与数据源
    add_subdirectory(extensions/dynamic/data_loader/csv)
    add_subdirectory(extensions/dynamic/data_loader/ulog)
    add_subdirectory(extensions/dynamic/data_loader/apbin)
    add_subdirectory(extensions/dynamic/data_loader/asc)
    add_subdirectory(extensions/dynamic/data_loader/blf)
    add_subdirectory(extensions/dynamic/data_loader/mcap)
    add_subdirectory(extensions/dynamic/data_loader/json)
    add_subdirectory(extensions/dynamic/data_loader/binary)
    add_subdirectory(extensions/dynamic/data_loader/mdf4)
    add_subdirectory(extensions/dynamic/data_loader/rosbag2)
    add_subdirectory(extensions/dynamic/data_source)

    # 内置扩展：业务组件在 extensions/builtin 聚合，base 只链接聚合 target
    add_subdirectory(extensions/builtin)

    # 底座 UI 与入口
    add_subdirectory(base)

    # 组件运行期资源部署：业务数据不写进 base。
    if(TARGET ps_gnss AND TARGET plotscope_app)
      add_custom_target(ps_deploy_gnss_db ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${PROJECT_SOURCE_DIR}/src/extensions/builtin/gnss/oem7_database.json
                $<TARGET_FILE_DIR:plotscope_app>/oem7_database.json
        DEPENDS plotscope_app)
    endif()
  else()
    message(WARNING "未找到 Qt6 Widgets，只构建内核（-DPS_BUILD_GUI=OFF 可静默）")
  endif()
endif()
