add_library(ps_base_plotting OBJECT
  include/plotting/PlottingModule.h
  manager/PlottingModule.cpp
  manager/PlottingSession.h
  manager/PlottingSession.cpp
  manager/PlottingCommands.cpp
  manager/PlottingSettings.cpp
  tabs/PlottingTabs.h
  tabs/PlottingTabs.cpp
  tabs/PlottingGrid.h
  canvas/PlottingCanvas.h
  canvas/PlottingCanvas.cpp
  ribbon/PlottingRibbon.h
  ribbon/PlottingRibbon.cpp
)
set_target_properties(ps_base_plotting PROPERTIES AUTOMOC ON)
target_include_directories(ps_base_plotting PUBLIC
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/base
  ${PROJECT_SOURCE_DIR}/src/base/modules/runtime/include
  ${PROJECT_SOURCE_DIR}/src/base/kernel/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(ps_base_plotting PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/manager
  ${CMAKE_CURRENT_SOURCE_DIR}/tabs
  ${CMAKE_CURRENT_SOURCE_DIR}/canvas
  ${CMAKE_CURRENT_SOURCE_DIR}/ribbon
  ${PROJECT_SOURCE_DIR}/src/base/modules/shell/ui
)
target_link_libraries(ps_base_plotting PUBLIC ps_host ps_kernel Qt6::Widgets SARibbonBar::SARibbonBar)
