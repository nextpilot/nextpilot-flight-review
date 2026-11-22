# ADS —— Qt Advanced Docking System（LGPL-2.1，5.1.1，vendored 源码）
# 只编共享库：LGPL 要求用户可替换，故不静态链进主程序。
file(GLOB ADS_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(APPEND ADS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/ads.qrc")
if(UNIX AND NOT APPLE)
  list(APPEND ADS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/linux/FloatingWidgetTitleBar.cpp")
endif()

set(QtADS_VERSION_MAJOR 5)
set(QtADS_VERSION_MINOR 1)
set(QtADS_VERSION_PATCH 1)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules/Versioning.cmake")
generate_ads_version_header()

add_library(ads SHARED ${ADS_SOURCES})
add_library(ads::ads ALIAS ads)
target_compile_definitions(ads PRIVATE ADS_SHARED_EXPORT)
target_include_directories(ads PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}")
# ADS 的 Unix 实现要读 Qt 私有 QPA 头；直接加私有头目录，避免拉 GuiPrivate 目标（本机缺 XKB 配置）
if(UNIX AND NOT APPLE AND Qt6Gui_INCLUDE_DIRS)
  list(GET Qt6Gui_INCLUDE_DIRS 0 _ads_gui_inc)
  target_include_directories(ads PRIVATE "${_ads_gui_inc}/${Qt6Gui_VERSION}/QtGui")
endif()
target_link_libraries(ads PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)
set_target_properties(ads PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  AUTOMOC ON
  AUTORCC ON
)
