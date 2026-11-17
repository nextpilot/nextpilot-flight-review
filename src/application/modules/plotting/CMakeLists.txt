add_library(ps_application_plotting OBJECT
  include/plotting/PlottingModule.h
  core/PlottingController.h
  core/PlottingController.cpp
  core/PlottingService.h
  widget/session/PlottingModule.cpp
  widget/session/PlottingSession.h
  widget/session/PlottingSession.cpp
  widget/session/PlottingCommands.cpp
  widget/settings/PlottingSettings.h
  widget/settings/PlottingSettings.cpp
  widget/tabs/PlottingTabs.h
  widget/tabs/PlottingTabs.cpp
  widget/tabs/PlottingLayout.h
  widget/tabs/PlottingLayout.cpp
  widget/canvas/PlottingCanvas.h
  widget/canvas/PlottingCanvas.cpp
  widget/ribbon/PlottingRibbon.h
  widget/ribbon/PlottingRibbon.cpp
)
set_target_properties(ps_application_plotting PROPERTIES AUTOMOC ON)
target_include_directories(ps_application_plotting PUBLIC
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/application
  ${PROJECT_SOURCE_DIR}/src/application/runtime/include
  ${PROJECT_SOURCE_DIR}/src/application/runtime/host/include
  ${PROJECT_SOURCE_DIR}/src/application/kernel/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(ps_application_plotting PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/core
  ${CMAKE_CURRENT_SOURCE_DIR}/widget/session
  ${CMAKE_CURRENT_SOURCE_DIR}/widget/tabs
  ${CMAKE_CURRENT_SOURCE_DIR}/widget/canvas
  ${CMAKE_CURRENT_SOURCE_DIR}/widget/ribbon
  ${CMAKE_CURRENT_SOURCE_DIR}/widget/settings
  ${PROJECT_SOURCE_DIR}/src/application/shell
)
target_link_libraries(ps_application_plotting PUBLIC ps_runtime ps_host ps_kernel ps_filter_ui Qt6::Widgets SARibbonBar::SARibbonBar)
