add_library(ps_application_plotting OBJECT
  include/plotting/PlottingModule.h
  manager/PlottingModule.cpp
  manager/PlottingSession.h
  manager/PlottingSession.cpp
  manager/PlottingCommands.cpp
  manager/PlottingSettings.h
  manager/PlottingSettings.cpp
  tabs/PlottingTabs.h
  tabs/PlottingTabs.cpp
  tabs/PlottingLayout.h
  tabs/PlottingLayout.cpp
  canvas/PlottingCanvas.h
  canvas/PlottingCanvas.cpp
  ribbon/PlottingRibbon.h
  ribbon/PlottingRibbon.cpp
)
set_target_properties(ps_application_plotting PROPERTIES AUTOMOC ON)
target_include_directories(ps_application_plotting PUBLIC
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/application
  ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include
  ${PROJECT_SOURCE_DIR}/src/application/kernel/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(ps_application_plotting PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/manager
  ${CMAKE_CURRENT_SOURCE_DIR}/tabs
  ${CMAKE_CURRENT_SOURCE_DIR}/canvas
  ${CMAKE_CURRENT_SOURCE_DIR}/ribbon
  ${PROJECT_SOURCE_DIR}/src/application/shell
)
target_link_libraries(ps_application_plotting PUBLIC ps_host ps_kernel ps_filter_ui Qt6::Widgets SARibbonBar::SARibbonBar)
