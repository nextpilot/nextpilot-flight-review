add_library(ps_runtime STATIC
  ModuleBridge.cpp
  SettingsStore.cpp
  LogService.cpp
)
target_include_directories(ps_runtime PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include
)
target_link_libraries(ps_runtime PUBLIC Qt6::Core)
